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
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>

#include <locale.h>
#include <langinfo.h>

#include <curl/curl.h>
#include <gcrypt.h>

#include "clamz.h"

static void print_pl_info(clamz_playlist *pl, const char* fname)
{
  clamz_meta_list *meta;

  printf("Playlist: %s\n", fname);
  if (pl->title)
    printf("* Title:    %s\n", pl->title);
  if (pl->creator)
    printf("* Creator:  %s\n", pl->creator);
  if (pl->image_name)
    printf("* Image:    %s\n", pl->image_name);
  for (meta = pl->meta; meta; meta = meta->next) {
    if (!strcmp(meta->urn, PMETA_ASIN))
      printf("* ASIN:     %s\n", meta->value);
    else if (!strcmp(meta->urn, PMETA_GENRE))
      printf("* Genre:    %s\n", meta->value);
    else
      printf("* '%s' = %s\n", meta->urn, meta->value);
  }
}

static void print_tr_info(clamz_track *tr, int n)
{
  clamz_meta_list *meta;

  printf("\n  Track %d:\n", n);
  if (tr->location)
    printf("  - URL:           %s\n", tr->location);
  if (tr->title)
    printf("  - Title:         %s\n", tr->title);
  if (tr->creator)
    printf("  - Creator:       %s\n", tr->creator);
  if (tr->album)
    printf("  - Album:         %s\n", tr->album);
  if (tr->image_name)
    printf("  - Image:         %s\n", tr->image_name);
  if (tr->duration)
    printf("  - Duration:      %s\n", tr->duration);
  if (tr->trackNum)
    printf("  - Track Number:  %s\n", tr->trackNum);
  for (meta = tr->meta; meta; meta = meta->next) {
    if (!strcmp(meta->urn, TMETA_ALBUM_ARTIST))
      printf("  - Album Artist:  %s\n", meta->value);
    else if (!strcmp(meta->urn, TMETA_ALBUM_ASIN))
      printf("  - Album ASIN:    %s\n", meta->value);
    else if (!strcmp(meta->urn, TMETA_ASIN))
      printf("  - ASIN:          %s\n", meta->value);
    else if (!strcmp(meta->urn, TMETA_DISC_NUM))
      printf("  - Disc Number:   %s\n", meta->value);
    else if (!strcmp(meta->urn, TMETA_FILE_SIZE))
      printf("  - File Size:     %s\n", meta->value);
    else if (!strcmp(meta->urn, TMETA_GENRE))
      printf("  - Genre:         %s\n", meta->value);
    else if (!strcmp(meta->urn, TMETA_PRODUCT_TYPE))
      printf("  - Product Type:  %s\n", meta->value);
    else if (!strcmp(meta->urn, TMETA_TRACK_TYPE))
      printf("  - File Type:     %s\n", meta->value);
    else
      printf("  - '%s' = %s\n", meta->urn, meta->value);
  }
}

static const char *getbasename(const char *fname)
{
  const char *b;

  while ((b = strchr(fname, '/')))
    fname = b + 1;

  return fname;
}

static int run_amz_file(clamz_downloader *dl,
			const clamz_config *cfg,
			FILE *amzfile,
			const char *fname)
{
  char *inbuf;
  unsigned char *xml;
  size_t sz;
  clamz_playlist *pl;
  int i;
  int status, rv = 0;
  char *logname;
  FILE *logfile;

  sz = 0;
  inbuf = NULL;

  while (!feof(amzfile) && !ferror(amzfile)) {
    if (inbuf)
      inbuf = realloc(inbuf, (sz + 1024) * sizeof(char));
    else
      inbuf = malloc((sz + 1024) * sizeof(char));
    sz += fread(&inbuf[sz], 1, 1024, amzfile);
  }

  if (amzfile != stdin)
    fclose(amzfile);

  if (cfg->printonly && cfg->printasxml) {
    xml = decrypt_amz_file(inbuf, sz, fname);
    if (!xml) {
      free(inbuf);
      return 2;
    }

    printf("%s", xml);
    free(xml);
    free(inbuf);
    return 0;
  }
  else {
    if (!cfg->printonly) {
      if (write_backup_file(inbuf, sz, getbasename(fname))) {
        free(inbuf);
        return 3;
      }
    }

    pl = new_playlist();

    if (read_amz_file(pl, inbuf, sz, fname)) {
      free(inbuf);
      free_playlist(pl);
      return 2;
    }

    if (!cfg->printonly) {
      logname = get_config_file_name("logs", getbasename(fname), ".log");
      if (!logname) {
        free(inbuf);
	return 1;
      }

      logfile = fopen(logname, "w");
      if (!logfile) {
	perror(logname);
	free(logname);
        free(inbuf);
	return 3;
      }

      free(logname);
      set_download_log_file(dl, logfile);
    }
    else {
      logfile = NULL;
    }

    if (cfg->printonly || cfg->verbose)
      print_pl_info(pl, fname);

    for (i = 0; i < pl->num_tracks; i++) {
      if (cfg->printonly || cfg->verbose)
        print_tr_info(pl->tracks[i], i + 1);

      status = download_track(dl, pl->tracks[i]);
      if (!rv)
        rv = status;
      fputc('\n', stderr);
    }

    set_download_log_file(dl, NULL);

    free(inbuf);
    free_playlist(pl);
    if (logfile)
      fclose(logfile);
    return rv;
  }
}

/* Parse XDG user-dirs configuration file and set environment
   variables (XDG_DESKTOP_DIR, XDG_MUSIC_DIR, etc.) */
static void set_xdg_user_dirs()
{
  const char *p, *q, *home;
  char *cfgfname;
  FILE *cfgfile;
  char buf[1024];
  char *envstr;

  home = getenv("HOME");
  if (!home)
    return;

  if ((p = getenv("XDG_CONFIG_HOME"))) {
    cfgfname = strdup(p);
    if (!cfgfname)
      return;
  }
  else {
    cfgfname = strdup(home);
    if (!cfgfname)
      return;
    if (concatenate(&cfgfname, "/.config", strlen("/.config"))) {
      free(cfgfname);
      return;
    }
  }

  if (concatenate(&cfgfname, "/user-dirs.dirs", strlen("/user-dirs.dirs"))) {
    free(cfgfname);
    return;
  }

  cfgfile = fopen(cfgfname, "r");
  free(cfgfname);
  if (!cfgfile)
    return;

  while (fgets(buf, sizeof(buf), cfgfile)) {
    p = buf;
    while (*p == ' ' || *p == '\t')
      p++;
    if (strncmp(p, "XDG_", 4))
      continue;

    q = p;
    while (isalnum((int) (unsigned char) *q) || *q == '_')
      q++;
    if (q == p || q[0] != '=' || q[1] != '"')
      continue;

    q++;
    envstr = NULL;
    if (concatenate(&envstr, p, q - p)) {
      free(envstr);
      fclose(cfgfile);
      return;
    }

    q++;
    if (!strncmp(q, "$HOME", 5)) {
      if (concatenate(&envstr, home, strlen(home))) {
	free(envstr);
	fclose(cfgfile);
	return;
      }
      q += 5;
    }

    while (*q && *q != '"') {
      if (q[0] == '\\' && q[1])
	q++;
      if (concatenate(&envstr, q, 1)) {
	free(envstr);
	fclose(cfgfile);
	return;
      }
      q++;
    }

    putenv(envstr);
  }

  fclose(cfgfile);
}

static int utf8locale;

int main(int argc, char **argv)
{
  clamz_config cfg;
  clamz_downloader *dl;
  char buf[256];
  FILE *amzfile;
  int err = 0;
  int i, n = 0;
  char *str;

  cfg.output_dir = cfg.name_format = cfg.forbid_chars = NULL;
  cfg.allowupper = cfg.allowutf8 = cfg.printonly = cfg.printasxml = 0;
  cfg.verbose = cfg.quiet = cfg.resume = 0;
  cfg.maxattempts = 5;

  /* Disable secure memory; we don't need it. */
  gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
  gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

  setlocale(LC_ALL, "");
  str = nl_langinfo(CODESET);
  if (!strcasecmp(str, "UTF-8") || !strcasecmp(str, "UTF8"))
    utf8locale = cfg.utf8locale = 1;
  else
    utf8locale = cfg.utf8locale = 0;

  set_xdg_user_dirs();

  if (parse_args(&argc, argv, &cfg)) {
    if (cfg.output_dir) free(cfg.output_dir);
    if (cfg.name_format) free(cfg.name_format);
    if (cfg.forbid_chars) free(cfg.forbid_chars);
    return 1;
  }

  if (!cfg.printonly)
    curl_global_init(CURL_GLOBAL_ALL);

  dl = new_downloader(&cfg);
  if (!dl)
    return 1;

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-")) {
      sprintf(buf, "clamz-stdin-%d", getpid());
      err = run_amz_file(dl, &cfg, stdin, buf);
    }
    else {
      amzfile = fopen(argv[i], "rb");
      if (!amzfile) {
	perror(argv[i]);
	err = 2;
	break;
      }
      err = run_amz_file(dl, &cfg, amzfile, argv[i]);
    }

    if (err)
      break;
    else
      n++;
  }

  free_downloader(dl);

  if (!cfg.printonly)
    curl_global_cleanup();

  if (cfg.output_dir) free(cfg.output_dir);
  if (cfg.name_format) free(cfg.name_format);
  if (cfg.forbid_chars) free(cfg.forbid_chars);

  if (!cfg.quiet && !cfg.printonly)
    fprintf(stderr, "%d of %d AMZ files downloaded successfully.\n",
	    n, argc - 1);

  return err;
}


void print_error(const char *message, ...)
{
  static char buf[4096];
  char *p, *q, *r;
  va_list ap;

  fprintf(stderr, "\rERROR: ");

  va_start(ap, message);
  vsnprintf(buf, 4095, message, ap);
  buf[4095] = 0;
  va_end(ap);

  p = q = buf;
  r = NULL;
  while (*p) {
    if (*p == '\n') {
      *p = 0;
      fputs(q, stderr);
      fputc('\n', stderr);
      fputc('\n', stderr);
      p++;
      q = p;
      r = NULL;
    }
    else if (p - q > 70 && r) {
      *r = 0;
      fputs(q, stderr);
      fputc('\n', stderr);
      p = q = r + 1;
      r = NULL;
    }
    else {
      if (*p == ' ')
	r = p;
      p++;
    }
  }
  if (*q) {
    fputs(q, stderr);
    fputc('\n', stderr);
  }

  fputc('\n', stderr);
}

void print_progress(const clamz_track *tr, const char *filename,
		    int progress)
{
  int i, j;
  const char *name;

  fputc('\r', stderr);

  if (tr->title)
    name = tr->title;
  else
    name = filename;

  for (i = j = 0; i < 32 && name[j]; i++) {
    if ((unsigned char) name[j] & 0x80) {
      if (!utf8locale)
	fputc('?', stderr);

      do {
	if (utf8locale)
	  fputc(name[j], stderr);
	j++;
      } while (((unsigned char) name[j] & 0xc0) == 0x80);
    }
    else {
      fputc(name[j], stderr);
      j++;
    }
  }

  for (; i < 32; i++)
    fputc(' ', stderr);

  if (progress >= 0) {
    fputs("  [", stderr);
    for (i = 0; i < (progress / 3); i++)
      fputc('#', stderr);
    for (; i < 33; i++)
      fputc(' ', stderr);
    fprintf(stderr, "]  %2d%% ", progress);
  }
  else {
    fprintf(stderr, "  ...");
  }

  fflush(stderr);
}
