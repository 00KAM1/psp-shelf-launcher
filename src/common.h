/*
 * Shelf - a home screen for a PSP library, built from what Sidequest,
 * Checkpoint and Emblem keep on the Memory Stick.
 * Shared definitions. Everything outside app.c and sys.c uses only sceIo*
 * and plain C, so it is built and tested on a PC as well.
 */
#ifndef SH_COMMON_H
#define SH_COMMON_H

#include <psptypes.h>

#define SH_VERSION_STR  "1.2"
#define SH_BUILD_STAMP  __DATE__ " " __TIME__
#ifndef SH_TAB_ALL
#define SH_TAB_ALL      "Library"             /* the name of the tab that holds every game */
#endif

#define SH_DIR          "ms0:/SHELF"
#define SH_CACHE_DIR    SH_DIR "/CACHE"
#define SH_LOG_PATH     SH_DIR "/shelf.log"
#define SH_CONFIG_PATH  SH_DIR "/config.txt"
#define SH_STATE_PATH   SH_DIR "/games.txt"
#define SH_LIB_PATH     SH_CACHE_DIR "/LIBRARY.BIN"

#define SQ_DIR          "ms0:/SIDEQUEST"
#define CP_JOURNEY_DIR  "ms0:/CHECKPOINT/JOURNEY"
#define EM_DIR          "ms0:/EMBLEM"

#define SH_PATH_MAX     192
#define SH_TITLE_MAX    96
#define SH_MAX_GAMES    600

/* What kind of program a library entry is. */
enum { GT_ISO = 0, GT_PSN, GT_HOMEBREW, GT_PS1, GT_SKIP = 255 };

/* One game as found on the Memory Stick. This struct is stored as-is in
 * LIBRARY.BIN, so only append fields and bump LIB_VERSION in scan.c. */
typedef struct {
    char path[SH_PATH_MAX];          /* the ISO, or the EBOOT.PBP */
    char title[SH_TITLE_MAX];        /* shown: the game's own title if plain text, else the file name tidied */
    char file_title[SH_TITLE_MAX];   /* the file or folder name, tidied */
    char id[16];                     /* DISC_ID, no dash, e.g. ULUS10558 */
    u8   type;                       /* GT_* */
    u8   has_icon, has_pic, title_ascii;
    u32  size, mtime;                /* to notice a changed file */
    u32  added;                      /* minutes since 2000 when Shelf first saw it */
    u32  key;                        /* crc32 of the path: the picture cache's file name */
} ShGame;

/* What the other plugins and Shelf itself know about a game. Built at start, never stored. */
#define SH_MAX_RECENT 5
typedef struct {
    /* Sidequest */
    char sq_dir[48];                 /* folder under ms0:/SIDEQUEST, "" = none */
    u8   sq_has[6];                  /* slot 0 = suspend, 1..5 = states */
    u32  sq_time[6];                 /* minutes since 2000 */
    s8   sq_newest;                  /* -1 = no slot */
    /* Checkpoint */
    u32  cp_secs;                    /* time played, seconds */
    int  cp_entries;
    char cp_last[64];                /* last journey entry (ASCII) */
    u32  cp_last_time;
    /* Emblem */
    s16  em_total, em_got;           /* em_total < 0: no game file */
    u8   em_nrecent;
    char em_recent[SH_MAX_RECENT][48];
    u32  em_recent_time[SH_MAX_RECENT];
    /* Shelf */
    u32  launched;                   /* last start from Shelf */
    u32  finished;                   /* 0 = not finished */
    u8   hidden;
    u8   fav;                        /* in Favourites */
    s8   pic;                        /* -1 auto, 0 suspend, 1..5 state, 6 icon */
    /* worked out */
    u32  last_played;
    u8   playing;
} ShInfo;

/* Shelf's own settings, ms0:/SHELF/config.txt */
typedef struct {
    int open_tab;                    /* 0 Playing, 1 All, 2 Finished, 3 where you left off, 4 Favourites */
    int last_tab;                    /* where you were when a game was started or Shelf closed */
    char last_cat[32];
    char last_game[SH_PATH_MAX];
    int confirm_exit;                /* ask before O leaves to the XMB */
    int return_to_shelf;             /* leaving a game comes back to Shelf, also when Shelf was opened from the XMB */
    int sort[4];                     /* per tab (Playing, All, Finished, Favourites), SORT_* */
    char all_default[32];            /* the category All opens on at every start */
    char fav_default[32];            /* the same for Favourites */
    int show_psn, show_homebrew, show_ps1;
    int show_hidden;
    int tab_home, tab_fav, tab_finished;   /* 0 = that tab is switched off (All always shows) */
    int titles_from_file;            /* 1 = always use the file name */
    int grid_cols;                   /* All: games per row, 3..6 */
    int pic_mode;                    /* Playing pictures: 0 newest Sidequest slot, 1 always the game's icon */
} ShConfig;

enum { SORT_LAST = 0, SORT_NAME, SORT_TIME, SORT_EMBLEM, SORT_ADDED, SORT_FINISHED, SORT_COUNT };

/* util.c */
int  sh_snprintf(char *buf, int size, const char *fmt, ...);
void sh_log(const char *fmt, ...);
void sh_strcpy(char *dst, int size, const char *src);
u32  sh_crc32(u32 crc, const void *data, u32 len);
int  sh_casecmp(const char *a, const char *b);
int  sh_ends_with(const char *s, const char *suffix);   /* case-insensitive */
u32  sh_minutes(int y, int mo, int d, int h, int mi);    /* minutes since 2000-01-01 */
u32  sh_parse_stamp(const char *s);                     /* "YYYY-MM-DD HH:MM" -> minutes, 0 = bad */
void sh_format_ago(u32 now, u32 then, char *out, int size);
void sh_format_played(u32 secs, char *out, int size);    /* "14h 22m" */
void sh_format_date(u32 minutes, char *out, int size, int with_time);   /* "Sun Nov 22 21:40" / "Nov 22" */
int  sh_utf8_ascii(const char *in, char *out, int size); /* 1 if all plain text; else best effort */
void sh_tidy_name(const char *file, char *out, int size);/* "Ys Seven (USA) [Undub].iso" -> "Ys Seven" */
int  sh_read_file(const char *path, char *buf, int size);/* bytes read, NUL-terminated, <0 error */
int  sh_mkdirs(void);

#endif
