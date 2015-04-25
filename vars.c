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
#include <ctype.h>

#include "clamz.h"

/* Convert a string according to user's preferences */
static char *convert_string(const clamz_config *cfg, const char *s)
{
  const unsigned char *us = (const unsigned char*) s;
  char *converted;
  int len;
  int i, j;

  len = strlen(s) + 1;
  converted = malloc(len * sizeof(char));

  if (!converted) {
    print_error("Out of memory");
    return NULL;
  }

  i = j = 0;
  while (s[i]) {
    if (j >= len - 1) {
      len *= 2;
      converted = realloc(converted, len * sizeof(char));

      if (!converted) {
	print_error("Out of memory");
	return NULL;
      }
    }

    if (us[i] & 0x80) {
      if (cfg->allowutf8) {
	converted[j++] = s[i++];
      }
      else {
	converted[j++] = '_';
	do
	  i++;
	while (((unsigned char) s[i] & 0xc0) == 0x80);
      }
    }
    else if (s[i] == '/' || iscntrl(us[i])) {
      converted[j++] = '_';
      i++;
    }
    else if (isupper(us[i]) && !cfg->allowupper) {
      converted[j++] = tolower(s[i++]);
    }
    else if (cfg->forbid_chars && strchr(cfg->forbid_chars, s[i])) {
      converted[j++] = '_';
      i++;
    }
    else {
      converted[j++] = s[i++];
    }
  }

  converted[j] = 0;

  return converted;
}

/* Get value of an environment variable or special format variable. */
static char *get_file_var(const clamz_config *cfg, const clamz_track *tr,
			  const char *var, int use_fallback)
{
  const char *s;
  const char *fallback = "Unknown";
  char nbuf[3];
  char *value;
  int subst_raw = 0;

  if (!strcasecmp(var, "title"))
    s = tr->title;
  else if (!strcasecmp(var, "creator"))
    s = tr->creator;
  else if (!strcasecmp(var, "album"))
    s = tr->album;
  else if (!strcasecmp(var, "tracknum")) {
    s = tr->trackNum;
    if (s && s[0] && !s[1]) {
      nbuf[0] = '0';
      nbuf[1] = s[0];
      nbuf[2] = 0;
      s = nbuf;
    }
    fallback = "00";
  }
  else if (!strcasecmp(var, "album_artist"))
    s = find_meta(tr->meta, TMETA_ALBUM_ARTIST);
  else if (!strcasecmp(var, "genre"))
    s = find_meta(tr->meta, TMETA_GENRE);
  else if (!strcasecmp(var, "discnum")) {
    s = find_meta(tr->meta, TMETA_DISC_NUM);
    fallback = "1";
  }
  else if (!strcasecmp(var, "suffix")) {
    s = find_meta(tr->meta, TMETA_TRACK_TYPE);
    fallback = "mp3";
  }
  else if (!strcasecmp(var, "asin"))
    s = find_meta(tr->meta, TMETA_ASIN);
  else if (!strcasecmp(var, "album_asin"))
    s = find_meta(tr->meta, TMETA_ALBUM_ASIN);
  else if (!strcasecmp(var, "amz_title"))
    s = tr->playlist->title;
  else if (!strcasecmp(var, "amz_creator"))
    s = tr->playlist->creator;
  else if (!strcasecmp(var, "amz_asin"))
    s = find_meta(tr->playlist->meta, PMETA_ASIN);
  else if (!strcasecmp(var, "amz_genre"))
    s = find_meta(tr->playlist->meta, PMETA_GENRE);
  else {
    s = getenv(var);
    fallback = "";
    subst_raw = 1;
  }

  if (!s || !s[0]) {
    if (use_fallback)
      s = fallback;
    else
      s = "";
  }

  if (subst_raw) {
    value = strdup(s);
    if (!value)
      print_error("Out of memory");
    return value;
  }
  else {
    return convert_string(cfg, s);
  }
}

/* Concatenate value of a variable onto a filename.  Add an extra '_'
   if necessary to avoid starting a file/directory name with a dot. */
static int concatenate_var(char **filename, const char *s)
{
  if (s[0] == '.'
      && (!*filename || !(*filename)[0]
	  || (*filename)[strlen(*filename) - 1] == '/')) {
    if (concatenate(filename, "_", 1))
      return 1;
  }

  return concatenate(filename, s, strlen(s));
}

/* Expand a variable reference and append to filename. */
static int expand_file_var(const clamz_config *cfg, const clamz_track *tr,
			   char **filename, char *var, const char *format)
{
  char *p, *value;

  if ((p = strchr(var, ':'))) {
    if (p[1] == '-') {
      /* ${VAR:-ALT} -- substitute VAR if defined, otherwise ALT */
      *p = 0;
      if (!(value = get_file_var(cfg, tr, var, 0)))
	return 1;

      if (value[0]) {
	if (concatenate_var(filename, value)) {
	  free(value);
	  return 1;
	}

	free(value);
	return 0;
      }
      else {
	free(value);
	return expand_file_name(cfg, tr, filename, p + 2);
      }
    }
    else if (p[1] == '+') {
      /* ${VAR:+ALT} -- substitute ALT if VAR is defined */
      *p = 0;
      if (!(value = get_file_var(cfg, tr, var, 0)))
	return 1;

      if (value[0]) {
	if (expand_file_name(cfg, tr, filename, p + 2)) {
	  free(value);
	  return 1;
	}
      }

      free(value);
      return 0;
    }
    else {
      print_error("Invalid expression '${%s}' in '%s'", var, format);
      return 1;
    }
  }
  else {
    if (!(value = get_file_var(cfg, tr, var, 1)))
      return 1;

    if (concatenate_var(filename, value)) {
      free(value);
      return 1;
    }

    free(value);
    return 0;
  }
}

/* Expand variable references in a filename format string, and append
   result to filename. */
int expand_file_name(const clamz_config *cfg, const clamz_track *tr,
		     char **filename, const char *format)
{
  const char *p, *q;
  const char *f = format;
  char *var;
  int n;

  while (*f) {
    if ((p = strchr(f, '$'))) {
      if (p != f
	  && concatenate(filename, f, p - f))
	return 1;

      p++;
      if (*p == '{') {
	p++;
	q = p;
	n = 1;
	while (*q && n) {
	  if (*q == '{')
	    n++;
	  else if (*q == '}')
	    n--;
	  q++;
	}
	if (n) {
	  print_error("Missing '}' in '%s'", format);
	  return 1;
	}
	q--;
	f = q + 1;
      }
      else {
	q = p;
	while (isalnum((int) (unsigned char) *q) || *q == '_')
	  q++;
	f = q;
      }

      if (p == q) {
	if (concatenate(filename, "$", 1))
	  return 1;
      }
      else {
	var = NULL;
	if (concatenate(&var, p, q - p))
	  return 1;
	if (expand_file_var(cfg, tr, filename, var, format)) {
	  free(var);
	  return 1;
	}
	free(var);
      }
    }
    else {
      return concatenate(filename, f, strlen(f));
    }
  }

  return 0;
}
