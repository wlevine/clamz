/*
 * clamz - Command-line downloader for the Amazon.com MP3 store
 * Copyright (c) 2008-2009 Benjamin Moody
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

#if __GNUC__ > 2
# define UNUSED __attribute__((unused))
# define PRINTF_ARG(n,m) __attribute__((format(printf, n, m)))
#else
# define UNUSED
# define PRINTF_ARG(n,m)
#endif

/* Known playlist metadata URNs */

#define PMETA_ASIN         "http://www.amazon.com/dmusic/ASIN"
#define PMETA_GENRE        "http://www.amazon.com/dmusic/primaryGenre"

/* Track metadata URNs */

#define TMETA_ALBUM_ARTIST "http://www.amazon.com/dmusic/albumPrimaryArtist"
#define TMETA_ALBUM_ASIN   "http://www.amazon.com/dmusic/albumASIN"
#define TMETA_ASIN         "http://www.amazon.com/dmusic/ASIN"
#define TMETA_DISC_NUM     "http://www.amazon.com/dmusic/discNum"
#define TMETA_FILE_SIZE    "http://www.amazon.com/dmusic/fileSize"
#define TMETA_GENRE        "http://www.amazon.com/dmusic/primaryGenre"
#define TMETA_PRODUCT_TYPE "http://www.amazon.com/dmusic/productTypeName"
#define TMETA_TRACK_TYPE   "http://www.amazon.com/dmusic/trackType"


typedef struct _clamz_meta_list {
  char *urn;
  char *value;
  struct _clamz_meta_list *next;
} clamz_meta_list;

typedef struct _clamz_track {
  struct _clamz_playlist *playlist;

  char *location;
  char *title;
  char *creator;
  char *album;
  char *image_name;
  char *duration;
  char *trackNum;
  clamz_meta_list *meta;
} clamz_track;

typedef struct _clamz_playlist {
  char *title;
  char *creator;
  char *image_name;
  clamz_meta_list *meta;

  int num_tracks;
  clamz_track **tracks;
} clamz_playlist;

typedef struct _clamz_config {
  char *output_dir;
  char *name_format;
  char *forbid_chars;
  unsigned allowupper : 1;
  unsigned allowutf8 : 1;
  unsigned utf8locale : 1;
  unsigned printonly : 1;
  unsigned printasxml : 1;
  unsigned verbose : 1;
  unsigned quiet : 1;
  unsigned resume : 1;
  int maxattempts;
} clamz_config;

typedef struct _clamz_downloader clamz_downloader;

/* playlist.c */
int concatenate(char **str, const char *add, int len);
clamz_playlist *new_playlist();
void free_playlist(clamz_playlist *pl);
const char *find_meta(const clamz_meta_list *meta, const char *urn);
unsigned char *decrypt_amz_file(const char *b64data,
                                unsigned long b64len, const char *fname);
int read_amz_file(clamz_playlist *pl, const char *b64data,
		  unsigned long b64len, const char *fname);
int write_backup_file(const char *b64data, unsigned long b64len,
		      const char *fname);

/* options.c */
char *get_config_file_name(const char *subdir, const char *name,
			   const char *suffix);
int parse_args(int *argc, char **argv, clamz_config *cfg);

/* vars.c */
int expand_file_name(const clamz_config *cfg, const clamz_track *tr,
		     char **filename, const char *format);

/* download.c */
clamz_downloader *new_downloader(const clamz_config *cfg);
void free_downloader(clamz_downloader *dl);
void set_download_log_file(clamz_downloader *dl, FILE *log);
int download_track(clamz_downloader *dl, clamz_track *tr);

/* clamz.c */
void print_error(const char *message, ...) PRINTF_ARG(1, 2);
void print_progress(const clamz_track *tr, const char *filename,
		    int progress);
