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
#include <sys/types.h>
#include <sys/stat.h>

#include "clamz.h"

char *get_config_file_name(const char *subdir, const char *base,
			   const char *suffix)
{
  char *home = getenv("HOME");
  char *s;
  int n;

  if (!home) {
    print_error("$HOME not defined");
    return NULL;
  }

  n = strlen(home) + strlen("/.clamz/") + strlen(base) + 1;
  if (subdir)
    n += strlen(subdir) + 1;
  if (suffix)
    n += strlen(suffix);

  s = malloc(n * sizeof(char));
  if (!s) {
    print_error("Out of memory");
    return NULL;
  }

  strcpy(s, home);
  strcat(s, "/.clamz");
  mkdir(s, 0775);

  if (subdir) {
    strcat(s, "/");
    strcat(s, subdir);
    mkdir(s, 0775);
  }

  strcat(s, "/");
  strcat(s, base);
  if (suffix)
    strcat(s, suffix);

  return s;
}

static int add_forbidden_chars(clamz_config *cfg, const char *addset)
{
  char *s;
  int n;

  while (*addset) {
    if (cfg->forbid_chars) {
      if (!strchr(cfg->forbid_chars, *addset)) {
	n = strlen(cfg->forbid_chars);
	s = malloc((n + 2) * sizeof(char));
	if (!s) {
	  print_error("Out of memory");
	  return 1;
	}

	strcpy(s, cfg->forbid_chars);
	s[n] = *addset;
	s[n + 1] = 0;
	free(cfg->forbid_chars);
	cfg->forbid_chars = s;
      }
    }
    else {
      s = malloc(2 * sizeof(char));
      if (!s) {
	print_error("Out of memory");
	return 1;
      }

      s[0] = *addset;
      s[1] = 0;
      cfg->forbid_chars = s;
    }
    addset++;
  }

  return 0;
}

static void remove_forbidden_chars(clamz_config *cfg, const char *delset)
{
  char *s;
  int n;

  if (!cfg->forbid_chars)
    return;

  while (*delset) {
    if ((s = strchr(cfg->forbid_chars, *delset))) {
      n = strlen(s);
      *s = s[n - 1];
      s[n - 1] = 0;
    }
    delset++;
  }
}

static void delchar(char *p)
{
  while (p[0]) {
    p[0] = p[1];
    if (p[0])
      p++;
  }
}

static char *checkcmd(char *buf, const char *cmd)
{
  char *p, *q;
  int n;

  p = buf;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;

  n = strlen(cmd);
  if (strncasecmp(p, cmd, n))
    return NULL;
  p += n;

  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;

  q = p;

  while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') {
    if (*q == '"') {
      delchar(q);
      while (*q && *q != '"') {
	if (*q == '\\')
	  delchar(q);
	q++;
      }
      delchar(q);
    }
    else
      q++;
  }

  *q = 0;
  return p;
}

static int read_user_config(clamz_config *cfg)
{
  static const char default_config[] =
    "## Clamz configuration file\n"
    "\n"
    "## Default format for output filenames.  This may contain any of\n"
    "## the following variables:\n"
    "##\n"
    "##  ${title} ${creator} ${album} ${tracknum} ${album_artist}\n"
    "##  ${genre} ${discnum} ${suffix} ${asin} ${album_asin}\n"
    "##\n"
    "## The name format may also contain slashes, if you'd like to\n"
    "## categorize your files in subdirectories.\n"
    "NameFormat       \"${tracknum} - ${title}.${suffix}\"\n"
    "\n"
    "## The base directory in which to store downloaded music.\n"
    "## If unset, it defaults to the current directory.\n"
    "# OutputDir       \"/home/me/Music\"\n"
    "\n"
    "## Set to True to allow uppercase in filenames.\n"
    "## False to convert to lowercase.\n"
    "AllowUppercase   True\n"
    "\n"
    "## Set to True to output UTF-8 filenames, False to output ASCII only,\n"
    "## UseLocale to check the system locale setting.\n"
    "AllowUTF8        UseLocale\n"
    "\n"
    "## The set of ASCII characters which are disallowed.  (Control\n"
    "## characters and slashes are always disallowed.)\n"
    "ForbidChars      \"!\\\"$*:;<>?\\\\`|~\"\n"
    "\n";

  char *cfgname;
  FILE *cfgfile;
  char buf[1024];
  int linenum = 0;
  char *p;

  cfgname = get_config_file_name(NULL, "config", NULL);
  if (!cfgname)
    return 1;

  cfgfile = fopen(cfgname, "r");
  if (!cfgfile) {
    cfgfile = fopen(cfgname, "w+");
    if (!cfgfile) {
      print_error("Unable to open configuration file '%s'", cfgname);
      free(cfgname);
      return 1;
    }

    fputs(default_config, cfgfile);
    fseek(cfgfile, 0L, SEEK_SET);
  }

  while (fgets(buf, sizeof(buf), cfgfile)) {
    linenum++;
    if ((p = strchr(buf, '#')))
      *p = 0;

    p = buf;
    while (*p == ' ' || *p == '\t' || *p == '\n')
      p++;
    if (!*p)
      continue;

    if ((p = checkcmd(buf, "NameFormat"))) {
      if (cfg->name_format)
	free(cfg->name_format);
      cfg->name_format = strdup(p);

      if (!cfg->name_format) {
	print_error("Out of memory");
	return 1;
      }
    }
    else if ((p = checkcmd(buf, "OutputDir"))) {
      if (cfg->output_dir)
	free(cfg->output_dir);
      cfg->output_dir = strdup(p);

      if (!cfg->output_dir) {
	print_error("Out of memory");
	return 1;
      }
    }
    else if ((p = checkcmd(buf, "ForbidChars"))) {
      if (cfg->forbid_chars)
	free(cfg->forbid_chars);
      cfg->forbid_chars = NULL;
      if (add_forbidden_chars(cfg, p))
	return 1;
    }
    else if ((p = checkcmd(buf, "AllowUppercase"))) {
      if (*p == 't' || *p == 'T')
	cfg->allowupper = 1;
      else
	cfg->allowupper = 0;
    }
    else if ((p = checkcmd(buf, "AllowUTF8"))) {
      if (*p == 't' || *p == 'T')
	cfg->allowutf8 = 1;
      else if (*p == 'f' || *p == 'F')
	cfg->allowutf8 = 0;
      else
	cfg->allowutf8 = cfg->utf8locale;
    }
  }

  fclose(cfgfile);
  free(cfgname);
  return 0;
}

static void print_usage(const char* progname)
{
  fprintf(stderr, "Usage: %s [options] amz-file ...\n"
	  "General options:\n"
	  " -o, --output=NAME:       write output to file NAME (may contain\n"
	  "                          variables, see below)\n"
	  " -d, --output-dir=DIR:    write output to directory DIR (may also\n"
	  "                          contain variables)\n"
	  " -r, --resume:            resume a partial download\n"
	  " -i, --info:              show info about AMZ-files; do not download\n"
	  "                          any tracks\n"
          " -x, --xml:               output XML data from AMZ-files; do not download\n"
          "                          any tracks\n"
	  " -v, --verbose:           display detailed information\n"
	  " -q, --quiet:             don't display non-critical messages\n"
	  " --help:                  display this help\n"
	  " --version:               display program version\n"
	  "\n"
	  "Output options:\n"
	  " --allow-chars=CHARS:     allow filenames containing CHARS\n"
	  " --forbid-chars=CHARS:    forbid filenames containing CHARS\n"
	  " --allow-uppercase:       allow uppercase letters in filenames\n"
	  " --forbid-uppercase:      forbid uppercase letters in filenames\n"
	  " --utf8-filenames:        allow UTF-8 filenames\n"
	  " --ascii-filenames:       force ASCII-only filenames\n"
	  "\n"
	  "Filenames (-o, -d) may contain the following variables:\n"
	  " ${title} ${creator} ${album} ${tracknum} ${album_artist} ${genre}\n"
	  " ${discnum} ${suffix} ${asin} ${album_asin}\n",
	  progname);
}

int parse_args(int *argc, char **argv, clamz_config *cfg)
{
  int i;
  int nfilenames = 0;

  read_user_config(cfg);

  for (i = 1; i < *argc; i++) {
    if (argv[i][0] != '-' || !argv[i][1]) {
      argv[nfilenames + 1] = argv[i];
      nfilenames++;
    }
    else if (argv[i][1] == '-' && argv[i][2] == 0) {
      i++;
      while (i < *argc) {
	argv[nfilenames + 1] = argv[i];
	nfilenames++;
	i++;
      }
      break;
    }
    else if (argv[i][1] != '-') {
      switch (argv[i][1]) {

      case 'd':
	if (cfg->output_dir)
	  free(cfg->output_dir);
	cfg->output_dir = NULL;

	if (argv[i][2])
	  cfg->output_dir = strdup(&argv[i][2]);
	else if (i == *argc - 1) {
	  fprintf(stderr, "%s: %s: requires argument\n",
		  argv[0], argv[i]);
	  print_usage(argv[0]);
	  return 1;
	}
	else {
	  i++;
	  cfg->output_dir = strdup(argv[i]);
	}

	if (!cfg->output_dir) {
	  print_error("Out of memory");
	  return 1;
	}

	break;

      case 'o':
	if (cfg->name_format)
	  free(cfg->name_format);
	cfg->name_format = NULL;

	if (argv[i][2])
	  cfg->name_format = strdup(&argv[i][2]);
	else if (i == *argc - 1) {
	  fprintf(stderr, "%s: %s: requires argument\n",
		  argv[0], argv[i]);
	  print_usage(argv[0]);
	  return 1;
	}
	else {
	  i++;
	  cfg->name_format = strdup(argv[i]);
	}

	if (!cfg->name_format) {
	  print_error("Out of memory");
	  return 1;
	}

	break;

      case 'r':
	cfg->resume = 1;
	break;

      case 'i':
	cfg->printonly = 1;
	break;
      
      case 'x':
        cfg->printonly = cfg->printasxml = 1;
        break;

      case 'v':
	cfg->verbose = 1;
	break;

      case 'q':
	cfg->quiet = 1;
	break;

      default:
	fprintf(stderr, "%s: unknown option %s\n",
		argv[0], argv[i]);
	print_usage(argv[0]);
	return 1;
      }
    }
    else if (!strcasecmp(argv[i], "--output-dir")) {
      if (i == *argc - 1) {
	fprintf(stderr, "%s: %s: requires argument\n",
		argv[0], argv[i]);
	print_usage(argv[0]);
	return 1;
      }
      i++;
      if (cfg->output_dir)
	free(cfg->output_dir);
      cfg->output_dir = strdup(argv[i]);

      if (!cfg->output_dir) {
	print_error("Out of memory");
	return 1;
      }
    }
    else if (!strncasecmp(argv[i], "--output-dir=", 13)) {
      if (cfg->output_dir)
	free(cfg->output_dir);
      cfg->output_dir = strdup(argv[i] + 13);

      if (!cfg->output_dir) {
	print_error("Out of memory");
	return 1;
      }
    }
    else if (!strcasecmp(argv[i], "--default-output-dir")) {
      if (i == *argc - 1) {
	fprintf(stderr, "%s: %s: requires argument\n",
		argv[0], argv[i]);
	print_usage(argv[0]);
	return 1;
      }
      i++;
      if (!cfg->output_dir)
	cfg->output_dir = strdup(argv[i]);

      if (!cfg->output_dir) {
	print_error("Out of memory");
	return 1;
      }
    }
    else if (!strncasecmp(argv[i], "--default-output-dir=", 21)) {
      if (!cfg->output_dir)
	cfg->output_dir = strdup(argv[i] + 21);

      if (!cfg->output_dir) {
	print_error("Out of memory");
	return 1;
      }
    }
    else if (!strcasecmp(argv[i], "--output")) {
      if (i == *argc - 1) {
	fprintf(stderr, "%s: %s: requires argument\n",
		argv[0], argv[i]);
	print_usage(argv[0]);
	return 1;
      }
      i++;
      if (cfg->name_format)
	free(cfg->name_format);
      cfg->name_format = strdup(argv[i]);

      if (!cfg->name_format) {
	print_error("Out of memory");
	return 1;
      }
    }
    else if (!strncasecmp(argv[i], "--output=", 9)) {
      if (cfg->name_format)
	free(cfg->name_format);
      cfg->name_format = strdup(argv[i] + 9);

      if (!cfg->name_format) {
	print_error("Out of memory");
	return 1;
      }
    }
    else if (!strcasecmp(argv[i], "--allow-chars")) {
      if (i == *argc - 1) {
	fprintf(stderr, "%s: %s: requires argument\n",
		argv[0], argv[i]);
	print_usage(argv[0]);
	return 1;
      }
      i++;
      remove_forbidden_chars(cfg, argv[i]);
    }
    else if (!strncasecmp(argv[i], "--allow-chars=", 14)) {
      remove_forbidden_chars(cfg, argv[i] + 14);
    }
    else if (!strcasecmp(argv[i], "--forbid-chars")) {
      if (i == *argc - 1) {
	fprintf(stderr, "%s: %s: requires argument\n",
		argv[0], argv[i]);
	print_usage(argv[0]);
	return 1;
      }
      i++;
      if (add_forbidden_chars(cfg, argv[i]))
	return 1;
    }
    else if (!strncasecmp(argv[i], "--forbid-chars=", 15)) {
      if (add_forbidden_chars(cfg, argv[i] + 15))
	return 1;
    }
    else if (!strcasecmp(argv[i], "--allow-uppercase"))
      cfg->allowupper = 1;
    else if (!strcasecmp(argv[i], "--forbid-uppercase"))
      cfg->allowupper = 0;
    else if (!strcasecmp(argv[i], "--utf8-filenames")
	     || !strcasecmp(argv[i], "--utf-8-filenames"))
      cfg->allowutf8 = 1;
    else if (!strcasecmp(argv[i], "--ascii-filenames"))
      cfg->allowutf8 = 0;
    else if (!strcasecmp(argv[i], "--resume"))
      cfg->resume = 1;
    else if (!strcasecmp(argv[i], "--info"))
      cfg->printonly = 1;
    else if (!strcasecmp(argv[i], "--xml"))
      cfg->printonly = cfg->printasxml = 1;
    else if (!strcasecmp(argv[i], "--verbose"))
      cfg->verbose = 1;
    else if (!strcasecmp(argv[i], "--quiet"))
      cfg->quiet = 1;
    else if (!strcasecmp(argv[i], "--help")) {
      print_usage(argv[0]);
      exit(0);
    }
    else if (!strcasecmp(argv[i], "--version")) {
      fprintf(stderr, "%s\n"
	      "Copyright (C) 2010 Benjamin Moody\n"
	      "This program is free software.  There is ABSOLUTELY NO WARRANTY\n"
	      "of any kind.  Please see COPYING for more details.\n"
	      "Please report bugs to %s.\n",
	      PACKAGE_STRING, PACKAGE_BUGREPORT);
      exit(0);
    }
    else {
      fprintf(stderr, "%s: unknown option %s\n",
	      argv[0], argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  *argc = nfilenames + 1;
  return 0;
}
