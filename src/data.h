#ifndef SH_DATA_H
#define SH_DATA_H
#include "common.h"
#include "scan.h"

/* Reads what Sidequest, Checkpoint, Emblem and Shelf itself keep, for every game. */
void data_load_all(const ShLibrary *lib, ShInfo *info, u32 now);
/* Works out last_played and playing from the rest. */
void data_finish(const ShGame *g, ShInfo *in);

/* Checkpoint's journey, newest first, for the info screen. */
typedef struct { u32 when; u32 secs; char text[64]; } ShEntry;
int  data_journey(const char *id, ShEntry *out, int max, int *total);

/* Sidequest picture path for slot k of a game (k: 0 suspend, 1..5 states). */
void data_thm_path(const ShInfo *in, int k, char *out, int size);

/* Shelf's own per-game notes (ms0:/SHELF/games.txt) and settings (config.txt). */
void state_save(const ShLibrary *lib, const ShInfo *info);
void config_load(ShConfig *c);
void config_save(const ShConfig *c);
void sq_resume_request(const char *path_or_null);
void sq_start_request(const char *path, int slot);          /* slot 0..5, or -1: start without loading and without Sidequest's question */

/* Time and battery since the PSP was last powered on (or restarted from cold). */
typedef struct { u32 start; int start_pct; u32 seen; int seen_pct;       /* times in minutes since 2000, -1 = no battery */
                 u32 asleep; int plugin; int bad; } ShUptime;                      /* plugin: Runtime keeps the record (all programs, sleep apart); Shelf only reads */
int  uptime_refresh(ShUptime *u, u32 now);                 /* reads Runtime's record again; 1 = Runtime is keeping it. u->bad: it is there but cannot be read, show nothing */
void uptime_begin(ShUptime *u, u32 now, int pct);          /* at start: carries on, or begins anew and logs the one before */
void uptime_mark(ShUptime *u, u32 now, int pct, int coming_back);   /* coming_back: Shelf is about to hand over (game, XMB, restart) */
void uptime_line(u32 from, int from_pct, u32 to, int to_pct, char *out, int size);
#endif
