#ifndef SH_UI_H
#define SH_UI_H
#include "common.h"
#include "scan.h"
#include "data.h"
#include "gfx.h"
#include "arkcfg.h"

/* Buttons, the PSP's own bit values */
#define B_SELECT 0x0001
#define B_START  0x0008
#define B_UP     0x0010
#define B_RIGHT  0x0020
#define B_DOWN   0x0040
#define B_LEFT   0x0080
#define B_L      0x0100
#define B_R      0x0200
#define B_TRI    0x1000
#define B_CIRCLE 0x2000
#define B_CROSS  0x4000
#define B_SQUARE 0x8000
/* the analog stick, kept apart from the d-pad: on All it moves freely through the grid */
#define B_AUP    0x10000
#define B_ARIGHT 0x20000
#define B_ADOWN  0x40000
#define B_ALEFT  0x80000
#define B_HOME   0x100000         /* the PS button */

/* What the main loop has to do after a button press. */
enum { ACT_NONE = 0, ACT_LAUNCH, ACT_XMB, ACT_USB, ACT_SLEEP, ACT_RESTART, ACT_SHUTDOWN, ACT_RECOVERY,
       ACT_RESCAN, ACT_REBUILD, ACT_REDRAW };

enum { OV_NONE = 0, OV_INFO, OV_SORT, OV_GAME, OV_SETTINGS, OV_CONFIRM, OV_MESSAGE, OV_USB, OV_PROGRESS, OV_CATS, OV_QUICK, OV_START };

#define MAX_CATS 24
#define TAB_FAV 3
#define MAX_PLUGINS 48
#define MAX_ITEMS 40

typedef struct {
    int kind;                     /* 0 choice, 1 action, 2 info */
    char label[40];
    char value[40];
    int id;                       /* what it is (see ui.c) */
    int arg;
    const char *desc;
} UiItem;

typedef struct {
    ShLibrary *lib;
    ShInfo *info;
    ShConfig *cfg;
    char ark_dir[128];            /* "ms0:/PSP/SAVEDATA/ARK_01234/" */
    char self[SH_PATH_MAX];       /* this program's EBOOT.PBP */
    int has_ark;
    u32 now;                      /* minutes since 2000 */
    char clock[24];               /* "14:05  BATT 82%" */

    int tab;                      /* 0 Playing, 1 All, 2 Finished */
    int list[4][SH_MAX_GAMES];    /* 0 Playing, 1 All, 2 Finished, 3 Favourites (shown between All and Finished) */
    int count[4];
    int sel[4], top[4];
    int cat;                      /* All tab: category shown */
    int ncats;
    char cat_name[MAX_CATS][32];
    int cat_kind[MAX_CATS];       /* 0 all, 1 PSP games, 2 homebrew, 3 PS1, 4 folder, 5 favourites, 6 homebrew in no folder, 7 PSP games in no folder */
    int cat_count[MAX_CATS];
    int fcat;                     /* the category the Favourites tab shows */
    int menu_cat[MAX_CATS], menu_cnt[MAX_CATS], menu_n;   /* the category menu as opened: which categories, how many games */      /* games in each, for the category menu */
    int up_hide;                                  /* Runtime's record is there but cannot be read: show no time or battery */
    u32 up_sleep, up_mins; int up_pct0, up_pct;   /* since power-on: minutes, battery then and now (-1 = not known) */
    int menu_on_pick;             /* the category menu opens on 'Pick one for me' (it was used last) */
    int as_launcher;              /* running as ARK's launcher (VBOOT.PBP), not as an app from the Game menu */
    int usb_from_quick;           /* USB mode was started from the PS-button menu */
    int set_focus;                /* settings: 0 = on the list of sections, 1 = inside the section */

    int overlay;
    int ov_sel, ov_top, ov_page;
    int sort_opts[SORT_COUNT + 1], nsort;
    char msg[3][64];
    int confirm_action;           /* what X does on the confirm box */
    int confirm_arg;
    int usb_state;                /* 0 off, 1 waiting, 2 connected */
    int prog_done, prog_total;
    char prog_name[64];

    /* settings */
    int set_sec;                  /* 0 Shelf, 1 Library, 2 ARK-5, 3 Plugins, 4 Power & USB */
    u32 ms_free_mb, ms_total_mb;  /* Memory Stick space, 0 = not known */
    u32 rng;
    UiItem items[MAX_ITEMS];
    int nitems;
    ShText *ark, *plug[2];
    ShPlugin plugins[MAX_PLUGINS];
    int plugin_file[MAX_PLUGINS];
    int nplugins;
    int ark_changed, launcher_on;

    /* info screen */
    ShEntry *journey;
    int njourney, journey_total;

    /* the result of a press */
    int act_game;                 /* library index for ACT_LAUNCH */
    int act_from;                 /* 0: as usual. 1: from Sidequest slot act_slot. 2: without loading ("Start from..." chose it) */
    int act_slot;
    /* "Start from...": the game's slots, newest first, then -1 for "without loading" */
    int start_slot[7], start_n;
    int dirty;                    /* something changed: redraw */
} ShUi;

void ui_init(ShUi *ui);
void ui_rebuild(ShUi *ui);        /* after the library or the info changed */
int  ui_input(ShUi *ui, u32 pressed);
void ui_draw(ShUi *ui, const ShSurf *s);
void ui_draw_launch(ShUi *ui, const ShSurf *s, int game);
void ui_draw_progress(ShUi *ui, const ShSurf *s, const char *title, int done, int total, const char *name);
int  ui_selected_game(ShUi *ui);  /* library index, -1 none */
void ui_message(ShUi *ui, const char *a, const char *b, const char *c);
int  ui_idle(ShUi *ui);           /* nothing pressed: get a picture ready for the next page; 1 = did some work */
void ui_remember(ShUi *ui);       /* notes the tab, category and game in the settings (not saved here) */
const char *ui_title(ShUi *ui, int g);
#endif
