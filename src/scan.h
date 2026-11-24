#ifndef SH_SCAN_H
#define SH_SCAN_H
#include "common.h"

typedef struct {
    ShGame *g;
    int n;
} ShLibrary;

/* Called while new or changed games are read: done of total, and its name. */
typedef void (*ShProgress)(int done, int total, const char *name);

/* Walks ms0:/ISO and ms0:/PSP/GAME (sub-folders included), reusing LIBRARY.BIN
 * for files that have not changed, and writes the picture cache for the rest.
 * self = this program's own EBOOT path (left out). force = read every game again.
 * Returns the number of games read anew. */
int  scan_library(ShLibrary *lib, const char *self, u32 now, int force, ShProgress cb);
void scan_free(ShLibrary *lib);

/* The picture cache: ms0:/SHELF/CACHE/<key>.IMG */
enum { PIC_ICON = 0, PIC_SMALL, PIC_BACK };
int  cache_read(u32 key, int which, u16 *out);   /* 0 = ok */
void cache_path(u32 key, char *out, int size);

/* Every game's 72x40 icon in one block (lib->n pictures, library order), read
 * from CACHE/SMALL.BIN in one go. NULL if it is missing; free() it. */
u16 *scan_small_load(const ShLibrary *lib);

/* The game's own background picture (PIC1.PNG) as it is in the game, read afresh: the
 * cache only holds a small copy. NULL if there is none; free() it. */
u8  *scan_load_pic(const ShGame *g, u32 *len);

/* For the tests. */
int  scan_read_game(ShGame *g, const char *path, int type_hint);
#endif
