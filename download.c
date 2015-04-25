/*
 * clamz - Command-line downloader for the Amazon.com MP3 store
 * Copyright (c) 2008-2010 Benjamin Moody
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <curl/curl.h>

#include "clamz.h"

struct _clamz_downloader {
  const clamz_config *cfg;
  CURL *curl;
  char *filename;
  int outfd;
  clamz_track *track;
  int last_progress;
  curl_off_t startpos;
  char error_buf[CURL_ERROR_SIZE];
  FILE *log_file;
};

/* Initialize downloader state */
clamz_downloader *new_downloader(const clamz_config *cfg)
{
  clamz_downloader *dl = malloc(sizeof(clamz_downloader));
  char *cookiejar;
  char useragent[100];

  if (!dl) {
    print_error("Out of memory");
    return NULL;
  }

  if (!cfg->printonly) {
    dl->curl = curl_easy_init();

    if (!dl->curl) {
      print_error("Unable to initialize curl");
      free(dl);
      return NULL;
    }

    curl_easy_setopt(dl->curl, CURLOPT_ERRORBUFFER, dl->error_buf);
    curl_easy_setopt(dl->curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(dl->curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(dl->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(dl->curl, CURLOPT_COOKIEFILE, "");

    sprintf(useragent, "Amazon MP3 Downloader (%s)", PACKAGE_STRING);
    curl_easy_setopt(dl->curl, CURLOPT_USERAGENT, useragent);

    cookiejar = get_config_file_name(NULL, "cookies", NULL);
    if (cookiejar) {
      curl_easy_setopt(dl->curl, CURLOPT_COOKIEJAR, cookiejar);
      free(cookiejar);
    }
  }
  else
    dl->curl = NULL;

  dl->cfg = cfg;
  dl->filename = NULL;
  dl->outfd = -1;
  dl->track = NULL;
  return dl;
}

/* Free downloader state */
void free_downloader(clamz_downloader *dl)
{
  if (dl->curl)
    curl_easy_cleanup(dl->curl);
  if (dl->filename)
    free(dl->filename);
  if (dl->outfd > -1)
    close(dl->outfd);
  free(dl);
}

/* Callback for debug info logging */
static int write_debug_info(CURL *curl UNUSED, curl_infotype type, char *text,
			    size_t length, void *data)
{
  clamz_downloader *dl = data;

  if (dl->log_file) {
    switch (type) {
    case CURLINFO_TEXT:
      fprintf(dl->log_file, "* ");
      fwrite(text, 1, length, dl->log_file);
      fflush(dl->log_file);
      break;

    case CURLINFO_HEADER_IN:
      fprintf(dl->log_file, "< ");
      fwrite(text, 1, length, dl->log_file);
      fflush(dl->log_file);
      break;

    case CURLINFO_HEADER_OUT:
      fprintf(dl->log_file, "> ");
      fwrite(text, 1, length, dl->log_file);
      fflush(dl->log_file);
      break;

    default:
      /* ignore other types of log message */
      break;
    }
  }

  return 0;
}

/* Write curl log to given file */
void set_download_log_file(clamz_downloader *dl, FILE *log)
{
  dl->log_file = log;

  if (dl->curl) {
    if (log) {
      curl_easy_setopt(dl->curl, CURLOPT_VERBOSE, 1);
      curl_easy_setopt(dl->curl, CURLOPT_DEBUGFUNCTION, write_debug_info);
      curl_easy_setopt(dl->curl, CURLOPT_DEBUGDATA, dl);
    }
    else {
      curl_easy_setopt(dl->curl, CURLOPT_VERBOSE, 0);
    }
  }
}

/* Create parent directories if they do not already exist */
static int create_parents(char *filename)
{
  char *p;

  p = filename;
  while (p && *p) {
    p = strchr(p + 1, '/');
    if (p) {
      *p = 0;
      if (mkdir(filename, 0777) && errno != EEXIST) {
	print_error("Cannot create directory %s: %s", filename,
		    strerror(errno));
	*p = '/';
	return 1;
      }
      *p = '/';
      p++;
    }
  }

  return 0;
}

/* Callback for writing downloaded data to the output file */
static size_t write_output(void *ptr, size_t size, size_t n, void *data)
{
  clamz_downloader *dl = data;
  int r;

  r = write(dl->outfd, ptr, size * n);
  if (r < 0) {
    print_error("Error writing to %s: %s", dl->filename, strerror(errno));
    return 0;
  }
  else
    return r;
}

/* Callback for displaying progress of transfer */
static int show_progress(void *data, double dltotal, double dlnow,
			 double ultotal UNUSED, double ulnow UNUSED)
{
  clamz_downloader *dl = data;
  int progress;

  if (dl->cfg->quiet)
    return 0;

  if (dltotal > 0) {
    dlnow += dl->startpos;
    dltotal += dl->startpos;
    progress = (int) (100 * dlnow / dltotal);
  }
  else {
    progress = -1;
  }

  if (progress != dl->last_progress) {
    dl->last_progress = progress;
    print_progress(dl->track, dl->filename, progress);
  }

  return 0;
}

int download_track(clamz_downloader *dl, clamz_track *tr)
{
  int i;
  char *s;
  CURLcode err;

  if (!tr->location) {
    print_error("No URL provided for this track");
    return 2;
  }

  dl->track = tr;

  if (dl->filename)
    free(dl->filename);
  dl->filename = NULL;

  /* ignore output_dir if name_format is an absolute path */
  if (dl->cfg->output_dir
      && (!dl->cfg->name_format || dl->cfg->name_format[0] != '/')) {
    if (expand_file_name(dl->cfg, tr, &dl->filename, dl->cfg->output_dir))
      return 1;
    if (expand_file_name(dl->cfg, tr, &dl->filename, "/"))
      return 1;
  }

  if (dl->cfg->name_format) {
    if (expand_file_name(dl->cfg, tr, &dl->filename, dl->cfg->name_format))
      return 1;
  }

  if (!dl->filename || !dl->filename[0]) {
    print_error("No output filename specified");
    return 1;
  }

  if (!dl->cfg->resume && !access(dl->filename, F_OK)) {
    s = malloc((strlen(dl->filename) + 10) * sizeof(char));
    if (!s) {
      print_error("Out of memory");
      return 1;
    }

    i = 1;
    do {
      sprintf(s, "%s.%d", dl->filename, i);
      i++;
    } while (!access(s, F_OK));

    print_error("\"%s\" already exists; renaming new file to \"%s\"",
		dl->filename, s);

    free(dl->filename);
    dl->filename = s;
  }

  if (dl->cfg->printonly) {
    printf("  Output to \"%s\"\n", dl->filename);
    free(dl->filename);
    dl->filename = NULL;
    return 0;
  }

  if (create_parents(dl->filename)) {
    free(dl->filename);
    dl->filename = NULL;
    return 4;
  }
 
  dl->outfd = open(dl->filename, O_WRONLY | O_APPEND | O_CREAT, 0666);

  if (dl->outfd < 0) {
    print_error("Unable to open \"%s\" (%s)", dl->filename, strerror(errno));

    free(dl->filename);
    dl->filename = NULL;
    return 4;
  }

  /* NOTE: there isn't any way to determine the file size before
     starting the download.
     - The file size declared in the amz file (TMETA_FILE_SIZE) is wrong.
     - Amazon's servers apparently forbid HEAD requests.
  */

  if (!dl->cfg->quiet)
    fprintf(stderr, "Downloading \"%s\"\n", dl->filename);

  i = 0;
  do {
    i++;
    dl->last_progress = -2;

    curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, write_output);
    curl_easy_setopt(dl->curl, CURLOPT_WRITEDATA, dl);

    curl_easy_setopt(dl->curl, CURLOPT_PROGRESSFUNCTION, show_progress);
    curl_easy_setopt(dl->curl, CURLOPT_PROGRESSDATA, dl);

    curl_easy_setopt(dl->curl, CURLOPT_URL, tr->location);

    dl->startpos = lseek(dl->outfd, (off_t) 0, SEEK_END);
    curl_easy_setopt(dl->curl, CURLOPT_RESUME_FROM_LARGE, dl->startpos);

    err = curl_easy_perform(dl->curl);

    if (!err) {
      /* success! */
      break;
    }

    if (dl->startpos != 0 && err == CURLE_HTTP_RANGE_ERROR) {
      /* assume that this means we've already downloaded the whole
	 thing... I guess */
      print_progress(tr, dl->filename, 100);
      err = 0;
      break;
    }

    print_error("Error downloading file: %s", dl->error_buf);
    if (i < dl->cfg->maxattempts)
      sleep(2);

  } while (i < dl->cfg->maxattempts);

  if (err) {
    close(dl->outfd);
    dl->outfd = -1;
    free(dl->filename);
    dl->filename = NULL;
    return 4;
  }

  if (close(dl->outfd)) {
    print_error("Error writing to %s", dl->filename);
    dl->outfd = -1;
    free(dl->filename);
    dl->filename = NULL;
    return 4;
  }

  dl->outfd = -1;
  free(dl->filename);
  dl->filename = NULL;
  return 0;
}
