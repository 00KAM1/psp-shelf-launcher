/* Draws the pictures in docs/images with Shelf's own code, from the Memory Stick
 * that doc_stick.py builds out of your own games' art. Run from a folder that
 * holds ms0/ and msx.bin; see render/make_doc_pictures.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/common.h"
#include "../../src/scan.h"
#include "../../src/data.h"
#include "../../src/ui.h"

u8 msx[2048];
static u32 g_fb[512 * 272];
static ShSurf g_s = { g_fb, 512 };
static ShUi ui;

static void save(const char *name);
static void shot(const char *name)
{
    for (int i = 0; i < 400 && ui_idle(&ui); i++) {}     /* every picture on the page ready */
    ui_draw(&ui, &g_s);
    save(name);
}

static void save(const char *name)
{
    char p[128];
    snprintf(p, sizeof(p), "out/%s.rgba", name);
    FILE *f = fopen(p, "wb");
    for (int y = 0; y < 272; y++) fwrite(g_fb + y * 512, 4, 480, f);
    fclose(f);
    printf("%s\n", name);
}

static int find(ShLibrary *lib, const char *needle)
{
    for (int i = 0; i < lib->n; i++) if (strstr(lib->g[i].path, needle)) return i;
    printf("no game called %s\n", needle);
    exit(2);
}

static void select_game(int tab, int g)
{
    for (int i = 0; i < ui.count[tab]; i++) if (ui.list[tab][i] == g) { ui.sel[tab] = i; return; }
    printf("game %d is not on tab %d\n", g, tab);
    exit(2);
}

int main(void)
{
    FILE *f = fopen("msx.bin", "rb");
    if (!f || fread(msx, 1, 2048, f) != 2048) { printf("msx.bin missing\n"); return 2; }
    fclose(f);
    system("mkdir -p out");

    ShLibrary lib = { 0 };
    u32 now = sh_minutes(2026, 11, 24, 14, 5);
    scan_library(&lib, "ms0:/PSP/GAME/SHELF/EBOOT.PBP", now, 0, NULL);
    ShInfo *info = calloc(lib.n, sizeof(ShInfo));
    data_load_all(&lib, info, now);
    ShConfig cfg;
    config_load(&cfg);
    memset(&ui, 0, sizeof(ui));
    ui.lib = &lib; ui.info = info; ui.cfg = &cfg; ui.now = now; ui.has_ark = 1;
    strcpy(ui.ark_dir, "ms0:/PSP/SAVEDATA/ARK_01234/");
    strcpy(ui.self, "ms0:/PSP/SAVEDATA/ARK_01234/VBOOT.PBP");
    strcpy(ui.clock, getenv("DOC_CLOCK") ? getenv("DOC_CLOCK") : "14:05  BATT 82%");    /* DOC_CLOCK: to try another reading */
    ui.as_launcher = 1;
    ui_init(&ui);
    int ys = find(&lib, "Felghana");
    printf("%d games, %d on Home, %d favourites, %d completed\n", lib.n, ui.count[0], ui.count[TAB_FAV], ui.count[2]);

    /* Home */
    select_game(0, ys);
    shot("home");
    ui_input(&ui, B_SELECT);
    for (int i = 0; i < 5; i++) ui_input(&ui, B_DOWN);      /* to the Picture row (the first row is "Start from...") */
    shot("options");
    ui_input(&ui, B_CIRCLE);

    /* a game whose newest slot is a state, and "Start from..." for it */
    int lunar = find(&lib, "Silver Star");
    select_game(0, lunar);
    shot("home-state");
    ui_input(&ui, B_SELECT);
    ui_input(&ui, B_CROSS);                              /* the first row: Start from... */
    shot("start-from");
    ui_input(&ui, B_CIRCLE); ui_input(&ui, B_CIRCLE);
    select_game(0, ys);

    ui_input(&ui, B_SQUARE);
    shot("info-emblem");
    ui_input(&ui, B_R);
    shot("info-journey");
    ui_input(&ui, B_CIRCLE);

    /* All */
    ui_input(&ui, B_R);
    ui.sel[1] = 8;
    shot("all");
    ui_input(&ui, B_TRI);
    shot("category-menu");
    ui_input(&ui, B_CIRCLE);

    /* the screen shown while a game starts, with the game's own picture at full size */
    ui_draw_launch(&ui, &g_s, find(&lib, "Crimson Gem"));
    save("loading");

    /* Favourites, Completed */
    ui_input(&ui, B_R);
    ui.sel[TAB_FAV] = 2;
    shot("favourites");
    ui_input(&ui, B_R);
    shot("completed");
    ui_input(&ui, B_R);

    /* the PS-button menu, with Runtime keeping the record */
    select_game(0, ys);
    ui.up_mins = 121; ui.up_sleep = 240; ui.up_pct0 = 100; ui.up_pct = 85; strcpy(ui.clock, "13:24  BATT 85%");
    ui_input(&ui, B_HOME);
    shot("ps-menu");
    ui_input(&ui, B_HOME);
    strcpy(ui.clock, getenv("DOC_CLOCK") ? getenv("DOC_CLOCK") : "14:05  BATT 82%");

    /* Settings > Shelf */
    ui_input(&ui, B_START);
    ui_input(&ui, B_CROSS);
    shot("settings");
    return 0;
}
