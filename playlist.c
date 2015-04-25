/*
 * clamz - Command-line downloader for the Amazon.com MP3 store
 * Copyright (c) 2008-2011 Benjamin Moody
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
#include <unistd.h>
#include <sys/stat.h>

#include <expat.h>
#include <gcrypt.h>

#include "clamz.h"

/* Create a new empty playlist */
clamz_playlist *new_playlist()
{
  clamz_playlist *pl = malloc(sizeof(clamz_playlist));

  if (!pl) {
    print_error("Out of memory");
    return NULL;
  }

  pl->title = pl->creator = pl->image_name = NULL;
  pl->meta = NULL;
  pl->num_tracks = 0;
  pl->tracks = NULL;

  return pl;
}

/* Add a metadata tag to the given list. */
static clamz_meta_list *add_meta(clamz_meta_list **mptr)
{
  clamz_meta_list *m = malloc(sizeof(clamz_meta_list));

  if (!m) {
    print_error("Out of memory");
    return NULL;
  }

  m->urn = m->value = NULL;
  m->next = *mptr;
  *mptr = m;
  return m;
}

/* Add a track to the given playlist. */
static clamz_track *add_track(clamz_playlist *pl)
{
  clamz_track *tr;
  clamz_track **ar;

  if (pl->tracks)
    ar = realloc(pl->tracks, (pl->num_tracks + 1) * sizeof(clamz_track *));
  else
    ar = malloc(sizeof(clamz_track *));
  if (!ar) {
    print_error("Out of memory");
    return NULL;
  }
  pl->tracks = ar;

  tr = malloc(sizeof(clamz_track));
  if (!tr) {
    print_error("Out of memory");
    return NULL;
  }

  pl->tracks[pl->num_tracks] = tr;
  pl->num_tracks++;

  tr->playlist = pl;
  tr->location = tr->creator = tr->album = tr->title
    = tr->image_name = tr->duration = tr->trackNum = NULL;
  tr->meta = NULL;

  return tr;
}

/* Free a metadata list. */
static void free_meta_list(clamz_meta_list *meta)
{
  clamz_meta_list *m;

  while (meta) {
    if (meta->urn) free(meta->urn);
    if (meta->value) free(meta->value);
    m = meta;
    meta = meta->next;
    free(m);
  }
}

/* Free a track and associated data */
static void free_track(clamz_track *tr)
{
  if (tr) {
    if (tr->location) free(tr->location);
    if (tr->creator) free(tr->creator);
    if (tr->album) free(tr->album);
    if (tr->title) free(tr->title);
    if (tr->image_name) free(tr->image_name);
    if (tr->duration) free(tr->duration);
    if (tr->trackNum) free(tr->trackNum);
    free_meta_list(tr->meta);
    free(tr);
  }
}

/* Free an entire playlist */
void free_playlist(clamz_playlist *pl)
{
  int i;

  if (pl) {
    if (pl->title) free(pl->title);
    if (pl->creator) free(pl->creator);
    if (pl->image_name) free(pl->image_name);
    free_meta_list(pl->meta);

    for (i = 0; i < pl->num_tracks; i++)
      free_track(pl->tracks[i]);
    if (pl->tracks) free(pl->tracks);

    free(pl);
  }
}

/* Search for a given metavalue key (URN) */
const char *find_meta(const clamz_meta_list *meta, const char *urn)
{
  while (meta) {
    if (meta->urn && !strcmp(meta->urn, urn))
      return meta->value;
    meta = meta->next;
  }

  return NULL;
}


/**************** AMZ file parsing ****************/

enum {
  UNKNOWN_TAG,
  ALBUM,
  CREATOR,
  DURATION,
  IMAGE,
  LOCATION,
  META,
  PLAYLIST,
  TITLE,
  TRACK,
  TRACKLIST,
  TRACKNUM
};
  
#define MAX_DEPTH 1024

struct parseinfo {
  const char *filename;
  XML_Parser parser;
  clamz_playlist *playlist;
  clamz_track *track;
  clamz_meta_list *meta;
  int stackdepth;
  int stack[MAX_DEPTH];
};

/* Append characters onto the end of the given string. */
int concatenate(char **str, const char *add, int len)
{
  int n;
  char *p;

  if (*str) {
    n = strlen(*str);
    p = realloc(*str, (n + len + 1) * sizeof(char));
  }
  else {
    n = 0;
    p = malloc((len + 1) * sizeof(char));
  }

  if (p) {
    *str = p;
    strncpy(&p[n], add, len);
    p[n+len] = 0;
    return 0;
  }
  else {
    print_error("Out of memory");
    return 1;
  }
}

/* Parser callback for a start tag */
static void handle_start_tag(void *data, const XML_Char *name,
			     const XML_Char **atts)
{
  struct parseinfo* pi = data;

  pi->stackdepth++;

  if (pi->stackdepth >= MAX_DEPTH) {
    print_error("Maximum stack depth exceeded while parsing AMZ file '%s'",
		pi->filename);
    XML_StopParser(pi->parser, 0);
    return;
  }

  if (!strcmp(name, "album"))
    pi->stack[pi->stackdepth] = ALBUM;
  else if (!strcmp(name, "creator"))
    pi->stack[pi->stackdepth] = CREATOR;
  else if (!strcmp(name, "duration"))
    pi->stack[pi->stackdepth] = DURATION;
  else if (!strcmp(name, "image"))
    pi->stack[pi->stackdepth] = IMAGE;
  else if (!strcmp(name, "location"))
    pi->stack[pi->stackdepth] = LOCATION;
  else if (!strcmp(name, "meta")) {
    if (pi->meta) {
      pi->stack[pi->stackdepth] = UNKNOWN_TAG;
    }
    else {
      pi->stack[pi->stackdepth] = META;

      if (pi->track)
	pi->meta = add_meta(&pi->track->meta);
      else
	pi->meta = add_meta(&pi->playlist->meta);

      if (pi->meta) {
	while (atts && atts[0]) {
	  if (!strcmp(atts[0], "rel")) {
	    pi->meta->urn = strdup(atts[1]);
	    break;
	  }
	  atts += 2;
	}
      }
    }
  }
  else if (!strcmp(name, "playlist"))
    pi->stack[pi->stackdepth] = PLAYLIST;
  else if (!strcmp(name, "title"))
    pi->stack[pi->stackdepth] = TITLE;
  else if (!strcmp(name, "track")) {
    if (pi->track) {
      pi->stack[pi->stackdepth] = UNKNOWN_TAG;
    }
    else {
      pi->stack[pi->stackdepth] = TRACK;
      pi->track = add_track(pi->playlist);
    }
  }
  else if (!strcmp(name, "tracklist"))
    pi->stack[pi->stackdepth] = TRACKLIST;
  else if (!strcmp(name, "trackNum"))
    pi->stack[pi->stackdepth] = TRACKNUM;
  else
    pi->stack[pi->stackdepth] = UNKNOWN_TAG;
}

/* Parser callback for an end tag */
static void handle_end_tag(void *data, const XML_Char *name UNUSED)
{
  struct parseinfo *pi = data;

  if (pi->stack[pi->stackdepth] == META)
    pi->meta = NULL;
  else if (pi->stack[pi->stackdepth] == TRACK)
    pi->track = NULL;

  pi->stackdepth--;
}

/* Parser callback for character data */
static void handle_chars(void* data, const XML_Char* s, int len)
{
  struct parseinfo* pi = data;

  switch (pi->stack[pi->stackdepth]) {
  case ALBUM:
    if (pi->track)
      concatenate(&pi->track->album, s, len);
    break;

  case CREATOR:
    if (pi->track)
      concatenate(&pi->track->creator, s, len);
    else
      concatenate(&pi->playlist->creator, s, len);
    break;

  case DURATION:
    if (pi->track)
      concatenate(&pi->track->duration, s, len);
    break;

  case IMAGE:
    if (pi->track)
      concatenate(&pi->track->image_name, s, len);
    else
      concatenate(&pi->playlist->image_name, s, len);
    break;

  case LOCATION:
    if (pi->track)
      concatenate(&pi->track->location, s, len);
    break;

  case META:
    if (pi->meta)
      concatenate(&pi->meta->value, s, len);
    break;

  case TITLE:
    if (pi->track)
      concatenate(&pi->track->title, s, len);
    else
      concatenate(&pi->playlist->title, s, len);
    break;

  case TRACKNUM:
    if (pi->track)
      concatenate(&pi->track->trackNum, s, len);
    break;
  }
}

/* Decode base64 data */
static unsigned char *base64_decode(unsigned long *output_len,
				    const char *input_buf,
				    unsigned long input_len,
				    const char *fname)
{
  unsigned char *result;
  unsigned long len, i;
  unsigned int bits = 0;
  int nch = 4;

  result = malloc(((input_len * 3 + 3) / 4) * sizeof(char));
  if (!result) {
    print_error("Out of memory");
    return NULL;
  }

  for (len = i = 0; i < input_len; i++) {
    if (input_buf[i] >= 'A' && input_buf[i] <= 'Z')
      bits |= (input_buf[i] - 'A');
    else if (input_buf[i] >= 'a' && input_buf[i] <= 'z')
      bits |= (input_buf[i] - 'a' + 26);
    else if (input_buf[i] >= '0' && input_buf[i] <= '9')
      bits |= (input_buf[i] - '0' + 52);
    else if (input_buf[i] == '+')
      bits |= 62;
    else if (input_buf[i] == '/')
      bits |= 63;
    else if (input_buf[i] <= ' ' || input_buf[i] == '=')
      continue;
    else {
      print_error("Invalid base64 data in AMZ file '%s'", fname);
      free(result);
      return NULL;
    }

    switch (--nch) {
    case 0:
      nch = 4;
      result[len++] = bits & 0xff;
      break;

    case 1:
      result[len++] = (bits >> 2) & 0xff;
      break;

    case 2:
      result[len++] = (bits >> 4) & 0xff;
      break;
    }

    bits <<= 6;
  }

  *output_len = len;
  return result;
}

/* Decrypt an AMZ file */
unsigned char *decrypt_amz_file(const char *b64data,
                                unsigned long b64len, const char *fname)
{
  static const unsigned char key[8] = { 0x29, 0xAB, 0x9D, 0x18,
					0xB2, 0x44, 0x9E, 0x31 };
  static const unsigned char initv[8] = { 0x5E, 0x72, 0xD7, 0x9A,
					  0x11, 0xB3, 0x4F, 0xEE };
  gcry_cipher_hd_t hd;
  gcry_error_t err;
  unsigned char *unpacked, *decrypted;
  unsigned long unpacked_len;
  unsigned long i;

  /* Some AMZ files are encrypted (and base64-encoded), while others
     are just plain XML.  Check if the start of the file looks like
     XML */
  i = 0;
  while (i < b64len && (b64data[i] <= ' ' || b64data[i] > '~'))
    i++;

  if (i < b64len && b64data[i] == '<') {
    /* assume file is not encrypted */
    decrypted = malloc(b64len + 1);
    if (!decrypted) {
      print_error("Out of memory");
      return NULL;
    }
    memcpy(decrypted, b64data, b64len);
    decrypted[b64len] = 0;
    return decrypted;
  }

  unpacked = base64_decode(&unpacked_len, b64data, b64len, fname);
  if (!unpacked)
    return NULL;

  if (unpacked_len % 8) {
    fprintf(stderr, "WARNING: length = %ld mod 8, discarding excess bytes\n",
	    (unpacked_len % 8));
    unpacked_len -= (unpacked_len % 8);
  }

  decrypted = malloc(unpacked_len+1);
  decrypted[unpacked_len] = 0; /* guard */

  if (!decrypted) {
    print_error("Out of memory");
    free(unpacked);
    return NULL;
  }

  if ((err = gcry_cipher_open(&hd, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_CBC, 0))) {
    print_error("Failed to initialize gcrypt (%s)", gcry_strerror(err));
    free(decrypted);
    free(unpacked);
    return NULL;
  }

  if ((err = gcry_cipher_setkey(hd, key, 8))) {
    print_error("Failed to set key (%s)", gcry_strerror(err));
    gcry_cipher_close(hd);
    free(decrypted);
    free(unpacked);
    return NULL;
  }
  
  if ((err = gcry_cipher_setiv(hd, initv, 8))) {
    print_error("Failed to set IV (%s)", gcry_strerror(err));
    gcry_cipher_close(hd);
    free(decrypted);
    free(unpacked);
    return NULL;
  }

  if ((err = gcry_cipher_decrypt(hd, decrypted, unpacked_len,
				 unpacked, unpacked_len))) {
    print_error("Unable to decrypt AMZ file '%s' (%s)", fname,
		gcry_strerror(err));
    gcry_cipher_close(hd);
    free(decrypted);
    free(unpacked);
    return NULL;
  }

  free(unpacked);

  gcry_cipher_close(hd);

  /* Remove any garbage characters from the end -- the files usually
     seem to be padded with 00 and/or 08 bytes; either way, expat
     doesn't like them. */
  for (i = unpacked_len; i > 0; i--) {
    if (decrypted[i - 1] == '\n' || decrypted[i] == '\r'
	|| decrypted[i - 1] >= ' ') {
      break;
    }
  }
  decrypted[i] = 0;

  return decrypted;
}

/* Read data from an AMZ file */
int read_amz_file(clamz_playlist *pl, const char *b64data,
		  unsigned long b64len, const char *fname)
{
  struct parseinfo pi;
  unsigned char *decrypted, *xml;
  unsigned long decrypted_len;
  int xerr;

  decrypted = decrypt_amz_file(b64data, b64len, fname);
  if (!decrypted)
    return 1;
  decrypted_len = strlen((char*) decrypted);

  pi.parser = XML_ParserCreate(NULL);
  if (!pi.parser) {
    print_error("Failed to initialize expat");
    free(decrypted);
    return 1;
  }

  XML_SetElementHandler(pi.parser, &handle_start_tag, &handle_end_tag);
  XML_SetCharacterDataHandler(pi.parser, &handle_chars);
  XML_SetUserData(pi.parser, &pi);

  pi.filename = fname;
  pi.playlist = pl;
  pi.track = NULL;
  pi.meta = NULL;
  pi.stackdepth = 0;

  /* copy decrypted data into XML parser buffer */
  xml = XML_GetBuffer(pi.parser, decrypted_len);
  memcpy(xml, decrypted, decrypted_len);

  if (!XML_ParseBuffer(pi.parser, decrypted_len, 1)) {
    xerr = XML_GetErrorCode(pi.parser);
    if (xerr != XML_ERROR_ABORTED) {
      print_error("Invalid XML (%s) in %s, line %d, column %d",
		  XML_ErrorString(xerr), fname,
		  (int) XML_GetCurrentLineNumber(pi.parser),
		  (int) XML_GetCurrentColumnNumber(pi.parser));
    }
    free(decrypted);
    XML_ParserFree(pi.parser);
    return 1;
  }

  free(decrypted);
  XML_ParserFree(pi.parser);
  return 0;
}

/* Save a backup amz file */
int write_backup_file(const char* b64data, unsigned long b64len,
		      const char* fname)
{
  char *name;
  FILE *f;

  name = get_config_file_name("amzfiles", fname, NULL);

  if (!name) {
    print_error("Unable to open configuration directory");
    return 1;
  }

  f = fopen(name, "wb");
  if (!f) {
    perror(name);
    free(name);
    return 1;
  }

  free(name);

  if (fwrite(b64data, 1, b64len, f) < b64len) {
    print_error("Unable to write backup file");
    fclose(f);
    return 1;
  }
  if (fclose(f)) {
    print_error("Unable to write backup file");
    return 1;
  }

  return 0;
}
