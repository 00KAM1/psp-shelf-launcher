#ifndef SH_ARKCFG_H
#define SH_ARKCFG_H
#include <psptypes.h>

/* A text file held as lines, changed line by line and written back whole.
 * Lines Shelf does not manage are kept exactly as they were. */
#define TF_MAX_LINES 256
#define TF_LINE 192
typedef struct { char line[TF_MAX_LINES][TF_LINE]; int n; char path[128]; int dirty; } ShText;

int  tf_load(ShText *t, const char *path);       /* a missing file is an empty one; 0 = ok */
int  tf_save(ShText *t);                          /* writes .NEW, then swaps it in; 0 = ok */

/* ---- ARK's SETTINGS.TXT ---- */
enum { ARK_CLOCK_GAME = 0, ARK_CLOCK_XMB, ARK_HIGHMEM, ARK_USBCHARGE, ARK_MSCACHE, ARK_ISOCACHE, ARK_SKIPLOGOS,
       ARK_HIDEPICS, ARK_HIDEMAC, ARK_HIDEDLC, ARK_NOLED, ARK_NOUMD, ARK_NOANALOG, ARK_WPA2, ARK_LAUNCHER, ARK_OPT_COUNT };
typedef struct { const char *label, *desc; int nvals; const char *vals[8]; } ArkOptInfo;
extern const ArkOptInfo g_ark_opts[ARK_OPT_COUNT];
int  ark_get(const ShText *t, int opt);           /* index into vals */
void ark_set(ShText *t, int opt, int val);

/* ---- PLUGINS.TXT ---- */
typedef struct { int line; char name[48]; char where[48]; int on; } ShPlugin;
int  plugins_list(const ShText *t, ShPlugin *out, int max);
void plugins_set(ShText *t, const ShPlugin *p, int on);

/* ---- Shelf as ARK's launcher (ARK_01234/VBOOT.PBP) ---- */
int  launcher_installed(const char *ark_dir);            /* 1 = VBOOT.PBP is Shelf */
int  launcher_install(const char *ark_dir, const char *self_eboot, char *msg, int msglen);
int  launcher_remove(const char *ark_dir, char *msg, int msglen);
int  launcher_return_arm(const char *ark_dir, const char *self_eboot);   /* before a game: come back to Shelf after it */
int  launcher_return_done(const char *ark_dir);                         /* at start: undo that; 1 = it was armed */
int  launcher_takeover(const char *ark_dir, const char *self_eboot);     /* ARK's launcher is on but is not Shelf: replace it; 1 = done */
int  launcher_update(const char *ark_dir, const char *self_eboot);    /* 1 = the start-up copy was refreshed */
/* Exiting to the XMB from launcher mode: turn the mode off until Shelf next starts. */
void launcher_pause(const char *ark_dir);
int  launcher_resume_if_paused(const char *ark_dir);   /* 1 = it was paused and is on again */
#endif
int  launcher_ready(const char *ark_dir, const char *self_eboot, char *vboot, int size);   /* 1 = VBOOT.PBP is Shelf; path in vboot */
