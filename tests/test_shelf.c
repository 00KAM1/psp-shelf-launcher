/* PC tests for Shelf: the library scan, picture cache, the plugins' files, ARK's
 * settings, and the screens (drawn with the real ui.c into PNGs under out/).
 * Run from tests/ after make_stick.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/common.h"
#include "../src/scan.h"
#include "../src/data.h"
#include "../src/ui.h"
#include "runtime_track/track.h"
#include "../src/arkcfg.h"
#include "../src/iso.h"
#include "../src/image.h"

u8 msx[2048];
static int fails;
#define CHECK(c, m) do { int _ok = (c); printf("%s %s\n", _ok ? "PASS" : "FAIL", m); if (!_ok) fails++; } while (0)

static u32 g_fb[512 * 272];
static ShSurf g_s = { g_fb, 512 };
static void shot(const char *name)
{
    char p[128];
    snprintf(p, sizeof(p), "out/%s.rgba", name);
    FILE *f = fopen(p, "wb");
    for (int y = 0; y < 272; y++) fwrite(g_fb + y * 512, 4, 480, f);
    fclose(f);
}

static int find(ShLibrary *lib, const char *needle)
{
    for (int i = 0; i < lib->n; i++) if (strstr(lib->g[i].path, needle)) return i;
    return -1;
}

static int progress_calls;
static void prog(int d, int t, const char *n) { progress_calls++; }

static void put_text(const char *path, const char *t) { FILE *f = fopen(path, "wb"); if (f) { fputs(t, f); fclose(f); } }
static void text_of(const char *path, char *buf, int size)
{
    FILE *f = fopen(path, "rb"); int n = f ? (int)fread(buf, 1, size - 1, f) : 0; if (f) fclose(f); buf[n] = 0;
}

/* Opens the category menu on All and chooses one by name, with the buttons. */
static void pick_cat(ShUi *ui, const char *name)
{
    int want = -1;
    for (int c = 0; c < ui->ncats; c++) if (!strcmp(ui->cat_name[c], name)) want = c;
    if (want < 0) { printf("FAIL no category called %s\n", name); return; }
    ui_input(ui, B_TRI);
    for (int i = 0; i < MAX_CATS + 2 && ui->ov_sel != 2 + want; i++) ui_input(ui, B_DOWN);
    ui_input(ui, B_CROSS);
}

int main(void)
{
    FILE *f = fopen("msx.bin", "rb");
    if (!f || fread(msx, 1, 2048, f) != 2048) { printf("msx.bin missing\n"); return 2; }
    fclose(f);
    system("mkdir -p out");

    /* ---- helpers ---- */
    char b[128];
    sh_tidy_name("ms0:/ISO/PSP/Ys Seven (USA) [Undub].iso", b, sizeof(b));
    CHECK(!strcmp(b, "Ys Seven"), "file names are tidied: tags and extension dropped");
    sh_tidy_name("ms0:/PSP/GAME/PORTS/Zelda_PSP", b, sizeof(b));
    CHECK(!strcmp(b, "Zelda PSP"), "underscores become spaces");
    CHECK(sh_parse_stamp("2026-11-22 21:40") == sh_minutes(2026, 11, 22, 21, 40) && sh_parse_stamp("2026-11-22") == 0, "date lines are read");
    sh_format_date(sh_minutes(2026, 11, 22, 21, 40), b, sizeof(b), 1);
    CHECK(!strcmp(b, "Sun Nov 22 21:40"), "dates are written with the weekday (2026-11-22 is a Sunday)");
    sh_format_ago(sh_minutes(2026, 11, 24, 14, 5), sh_minutes(2026, 11, 22, 21, 40), b, sizeof(b));
    CHECK(!strcmp(b, "Yesterday") || !strcmp(b, "2 days ago"), "'ago' text");
    CHECK(sh_utf8_ascii("英雄伝説", b, sizeof(b)) == 0 && !strcmp(b, "????"), "Japanese text is noticed (one ? per character)");
    u8 lz[64] = { 0x40, 'a', 'b', 'c', 'd', 0x04, 0x00, 0x50, 'x', 'y', 'z', 'q', '!' }, lz2[8] = { 0x14, 'a', 0x01, 0x00 };
    u8 lo[64];
    int ln = sh_lz4_block(lz, 13, lo, 64), ln2 = sh_lz4_block(lz2, 4, lo + 32, 32);
    CHECK(ln == 13 && !memcmp(lo, "abcdabcdxyzq!", 13) && ln2 == 9 && !memcmp(lo + 32, "aaaaaaaaa", 9), "LZ4 blocks decode (incl. an overlapping match)");

    /* ---- the scan ---- */
    ShLibrary lib = { 0 };
    u32 now = sh_minutes(2026, 11, 24, 14, 5);
    int fresh = scan_library(&lib, "ms0:/PSP/GAME/SHELF/EBOOT.PBP", now, 0, prog);
    int shown = 0;
    for (int i = 0; i < lib.n; i++) if (lib.g[i].type != GT_SKIP) { shown++; printf("  %-9s %d %-44s %s\n", lib.g[i].id, lib.g[i].type, lib.g[i].title, lib.g[i].path + 5); }
    CHECK(shown == 12, "12 games found: 7 ISO/CSO/ZSO, 2 ports, PS1, PSN, psp5 (sub-folders included)");
    CHECK(find(&lib, "SHELF") < 0 && find(&lib, "PARAM.PBP") < 0 && find(&lib, "PBOOT") < 0 && find(&lib, "%__SCE") < 0,
          "Shelf itself, DLC, an update-only folder and a 1.50 folder are not in the library");
    int up = find(&lib, "UPDATER"), vid = find(&lib, "VIDEO");
    CHECK(up >= 0 && lib.g[up].type == GT_SKIP && vid >= 0 && lib.g[vid].type == GT_SKIP, "an EBOOT of another category and a UMD video are kept out of sight (and not read again)");
    int ys = find(&lib, "Felghana"), zero = find(&lib, "Zero no"), sh = find(&lib, "Shining"), zel = find(&lib, "ZeldaPSP"),
        ct = find(&lib, "Chrono"), sc = find(&lib, "NPUH10191"), sn = find(&lib, "Snatc"), te = find(&lib, "Tetris"), p3 = find(&lib, "Persona");
    CHECK(ys >= 0 && !strcmp(lib.g[ys].title, "Ys: The Oath in Felghana") && !strcmp(lib.g[ys].id, "ULUS10558"), "ISO: title and disc ID from PARAM.SFO (dash removed)");
    CHECK(zero >= 0 && !strcmp(lib.g[zero].title, "Legend of Heroes Zero no Kiseki") && !strcmp(lib.g[zero].id, "NPJH50311"),
          "CSO with a Japanese title: read through deflate, shown by its file name");
    CHECK(sh >= 0 && !strcmp(lib.g[sh].id, "NPJH50342") && lib.g[sh].has_icon && lib.g[sh].has_pic, "ZSO: read through LZ4, icon and background cached");
    CHECK(zel >= 0 && lib.g[zel].type == GT_HOMEBREW && !strcmp(lib.g[zel].title, "The Legend of Zelda: A Link to the Past") && !lib.g[zel].id[0],
          "homebrew port in PSP/GAME/PORTS: title kept, SDK's default ID dropped");
    CHECK(ct >= 0 && lib.g[ct].type == GT_PS1 && sc >= 0 && lib.g[sc].type == GT_PSN, "PS1 (ME) and PSN (EG) EBOOTs recognised");
    CHECK(sn >= 0 && !lib.g[sn].has_icon, "a homebrew without an icon is still listed");
    CHECK(te >= 0 && !strcmp(lib.g[te].title, "TETRIS"), "the (R) mark is dropped from titles");
    u16 *icon = malloc(ICON_W * ICON_H * 2);
    CHECK(cache_read(lib.g[ys].key, PIC_ICON, icon) == 0 && icon[40 * ICON_W + 72] != 0, "the picture cache holds the icon");
    CHECK(cache_read(lib.g[ct].key, PIC_ICON, icon) == 0 && icon[40 * ICON_W + 2] == 0 && icon[40 * ICON_W + 72] != 0,
          "a square PS1 icon is centred, not stretched");
    CHECK(fresh == 14 && progress_calls > 0, "first scan reads every file once (12 games + 2 that are not)");
    ShLibrary lib2 = { 0 };
    int fresh2 = scan_library(&lib2, "ms0:/PSP/GAME/SHELF/EBOOT.PBP", now + 60, 0, NULL);
    CHECK(fresh2 == 0 && lib2.n == lib.n && lib2.g[ys].added == now, "second scan reuses LIBRARY.BIN: nothing read again, dates kept");
    scan_free(&lib2);
    { u16 *pk = scan_small_load(&lib); u16 one[SMALL_W * SMALL_H];
      CHECK(pk && cache_read(lib.g[ys].key, PIC_SMALL, one) == 0 && !memcmp(pk + ys * SMALL_W * SMALL_H, one, sizeof(one)),
            "every small icon is also in CACHE/SMALL.BIN (one read at start instead of one file per game)");
      free(pk); }
    { FILE *o = fopen("ms0/SHELF/CACHE/DEADBEEF.IMG", "wb"); fputs("old", o); fclose(o);
      ShLibrary l3 = { 0 };
      scan_library(&l3, "ms0:/PSP/GAME/SHELF/EBOOT.PBP", now, 0, NULL);
      o = fopen("ms0/SHELF/CACHE/DEADBEEF.IMG", "rb");
      CHECK(o != NULL, "an unchanged library leaves the cache folder alone");
      if (o) fclose(o);
      char tp[256]; snprintf(tp, sizeof(tp), "ms0%s", lib.g[te].path + 4);
      CHECK(rename(tp, "ms0/Tetris.away") == 0, "(a game is taken off the stick)");
      scan_library(&l3, "ms0:/PSP/GAME/SHELF/EBOOT.PBP", now, 0, NULL);
      o = fopen("ms0/SHELF/CACHE/DEADBEEF.IMG", "rb");
      char cp[64]; cache_path(lib.g[te].key, cp, sizeof(cp)); memmove(cp + 3, cp + 4, strlen(cp + 4) + 1);
      FILE *o2 = fopen(cp, "rb");
      CHECK(o == NULL && o2 == NULL, "when a game goes, its cached pictures (and stray ones) are removed");
      if (o) fclose(o); if (o2) fclose(o2);
      rename("ms0/Tetris.away", tp);
      scan_free(&l3);
      scan_free(&lib);
      scan_library(&lib, "ms0:/PSP/GAME/SHELF/EBOOT.PBP", now, 0, NULL);
      ys = find(&lib, "Felghana"); zero = find(&lib, "Zero no"); sh = find(&lib, "Shining"); zel = find(&lib, "ZeldaPSP");
      ct = find(&lib, "Chrono"); sc = find(&lib, "NPUH10191"); sn = find(&lib, "Snatc"); te = find(&lib, "Tetris"); p3 = find(&lib, "Persona");
      CHECK(te >= 0 && lib.g[te].has_icon, "  ... and made again when it comes back"); }

    /* ---- the plugins' files ---- */
    ShInfo *info = calloc(lib.n, sizeof(ShInfo));
    data_load_all(&lib, info, now);
    ShInfo *y = &info[ys];
    CHECK(y->sq_has[0] && y->sq_has[2] && !y->sq_has[1] && y->sq_newest == 0 && !strcmp(y->sq_dir, "YS_THE_OATH_IN_FELGH_3A1F"),
          "Sidequest: suspend and state 2 found by the game's path, suspend is the newest");
    CHECK(y->cp_secs == 51720 && y->cp_entries == 7 && !strcmp(y->cp_last, "Town of Redmont"), "Checkpoint: 14h 22m played, 7 journey entries (a broken line skipped), last one");
    CHECK(y->em_total == 89 && y->em_got == 6, "Emblem: 89 with a rule ('none' not counted); 6 earned (one no longer in the file not counted)");
    CHECK(y->em_nrecent == 5 && !strcmp(y->em_recent[0], "The Crystalline Entity") && !strcmp(y->em_recent[4], "For a Job Well Done"),
          "Emblem: newest unlocks first");
    CHECK(y->playing && info[sh].playing && info[p3].playing && !info[zel].playing, "Playing: games with a slot or a journey; not untouched ones");
    CHECK(!strcmp(info[sh].cp_last, "オカミ Lv12 Windaria Village"), "Japanese in a journey line is kept as written (drawn with the Japanese font)");
    { char o[64]; u8_cut("零の軌跡ABC", 4, o, sizeof(o)); CHECK(!strcmp(o, "零の軌."), "text is cut by characters, never inside one"); }
    ShEntry je[10]; int total;
    int nj = data_journey("ULUS10558", je, 10, &total);
    CHECK(nj == 7 && total == 7 && !strcmp(je[0].text, "Town of Redmont") && !strcmp(je[6].text, "Port of Redmont") && je[1].secs == 12 * 3600 + 58 * 60,
          "journey entries for the info screen, newest first");

    /* ---- the screens ---- */
    ShConfig cfg;
    config_load(&cfg);
    ShUi ui;
    memset(&ui, 0, sizeof(ui));
    ui.lib = &lib; ui.info = info; ui.cfg = &cfg; ui.now = now; ui.has_ark = 1;
    strcpy(ui.ark_dir, "ms0:/PSP/SAVEDATA/ARK_01234/");
    strcpy(ui.self, "ms0:/PSP/GAME/SHELF/EBOOT.PBP");
    strcpy(ui.clock, "14:05  BATT 82%");
    CHECK(!strcmp(cfg.all_default, "PSP Games") && !strcmp(cfg.fav_default, "All Games") && cfg.sort[3] == SORT_NAME,
          "as shipped: All opens on PSP Games, Favourites on All Games");
    ui_init(&ui);
    CHECK(!strcmp(ui.cat_name[ui.cat], "PSP") && ui.count[1] == 5 && ui.fcat == 0,
          "  ... with a PSP folder on the stick there is no 'PSP Games', so All opens on PSP (5 games)");
    strcpy(cfg.all_default, "All Games"); ui_init(&ui);          /* the checks below walk through All Games */
    CHECK(cfg.open_tab == 3 && ui.tab == 0 && ui.count[0] == 4 && ui.list[0][0] == ys, "first start opens on Playing; Felghana (played last) first of 4");
    ui_draw(&ui, &g_s); shot("01-playing");
    ui_input(&ui, B_RIGHT); ui_input(&ui, B_RIGHT);
    ui_draw(&ui, &g_s); shot("02-playing-icon");
    ui_input(&ui, B_R);
    CHECK(ui.tab == 1 && ui.count[1] == 12, "R: All, 12 games");
    printf("  categories:"); for (int c = 0; c < ui.ncats; c++) printf(" [%s %d]", ui.cat_name[c], ui.cat_count[c]); printf("\n");
    CHECK(ui.ncats == 7 && !strcmp(ui.cat_name[0], "All Games") && !strcmp(ui.cat_name[1], "MINIS") && !strcmp(ui.cat_name[2], "Other Homebrew") &&
          !strcmp(ui.cat_name[3], "Other PSP Games") && !strcmp(ui.cat_name[4], "PORTS") && !strcmp(ui.cat_name[5], "PSP") && !strcmp(ui.cat_name[6], "PSX"),
          "categories: All Games, then in name order: one per folder (MINIS, PORTS, PSP, PSX) and the two for games in no folder");
    { int in_some = 0; for (int c = 1; c < ui.ncats; c++) in_some += ui.cat_count[c];
      CHECK(ui.cat_count[4] == 2 && ui.cat_count[2] == 1 && ui.cat_count[3] == 2 && in_some == 12,
            "  ... no 'PSP Games' beside the PSP folder and no 'PS1' beside PSX; the root ISO and the PSN game are 'Other PSP Games'; every game is in one category"); }
    ui_draw(&ui, &g_s); shot("03-all");
    CHECK(cfg.grid_cols == 6, "All: 6 per row unless set");
    for (int gc = 3; gc <= 5; gc++) {               /* the other layouts; last game selected so names and scrolling show */
        cfg.grid_cols = gc; ui.top[1] = 0; ui.sel[1] = gc == 3 ? 4 : 0;
        char nm[32]; sprintf(nm, "03-all-%d-per-row", gc);
        ui_draw(&ui, &g_s); shot(nm);
    }
    cfg.grid_cols = 3; ui.sel[1] = 11; ui_draw(&ui, &g_s);
    CHECK(ui.top[1] == 2, "3 per row: 2 rows shown, scrolled to the last game");
    shot("03-all-3-per-row-end");
    { cfg.grid_cols = 4; ui.sel[1] = 0; ui.top[1] = 0; ui_draw(&ui, &g_s);
      int work = 0; while (ui_idle(&ui) && work < 100) work++;
      CHECK(work == 0, "4 per row: a page that is on screen needs no more work when idle");
      cfg.grid_cols = 3; ui_draw(&ui, &g_s);
      work = 0; while (ui_idle(&ui) && work < 100) work++;
      CHECK(work == 6, "3 per row: while idle, the next page's icons (6 here) are made ready before you scroll"); }
    cfg.grid_cols = 6; ui.sel[1] = 0; ui.top[1] = 0;
    /* the grid: UP and DOWN move by a row, they no longer change category */
    ui_input(&ui, B_DOWN);
    CHECK(ui.cat == 0 && ui.sel[1] == 6, "DOWN on All: one row down (6 per row), same category");
    ui_input(&ui, B_RIGHT); ui_input(&ui, B_UP);
    CHECK(ui.sel[1] == 1, "RIGHT, then UP: back on the first row, one along");
    ui_input(&ui, B_DOWN); ui_input(&ui, B_ADOWN);
    CHECK(ui.sel[1] == 7, "DOWN on the last row stays put (the stick does the same as the d-pad)");
    /* categories: a menu on Triangle */
    ui_input(&ui, B_TRI);
    CHECK(ui.overlay == OV_CATS && ui.ov_sel == 2, "Triangle on All: the menu, on the category in use");
    ui_draw(&ui, &g_s); shot("04-category-menu");
    ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN);
    ui_draw(&ui, &g_s); shot("04-category-menu-ports");
    ui_input(&ui, B_CROSS);
    CHECK(ui.overlay == OV_NONE && ui.cat == 4 && ui.count[1] == 2, "choose PORTS: the two ports");
    ui_draw(&ui, &g_s); shot("04-all-ports");
    ui_input(&ui, B_TRI);
    CHECK(ui.ov_sel == 6, "the menu opens on PORTS now");
    ui_input(&ui, B_RIGHT);
    CHECK(cfg.sort[1] == SORT_TIME && ui.overlay == OV_CATS, "LEFT/RIGHT in the menu changes the order (Name -> Time played) and it stays open");
    ui_input(&ui, B_LEFT);
    ui_input(&ui, B_CIRCLE);
    pick_cat(&ui, "Other Homebrew");
    CHECK(ui.count[1] == 1 && !strcmp(ui_title(&ui, ui.list[1][0]), "psp5"), "Other Homebrew: psp5");
    pick_cat(&ui, "All Games");
    CHECK(ui.cat == 0 && ui.count[1] == 12, "back to All Games");
    /* mark Crisis Core finished from All */
    int cc = find(&lib, "Crisis");
    for (int i = 0; i < ui.count[1]; i++) if (ui.list[1][i] == cc) ui.sel[1] = i;
    ui_input(&ui, B_SELECT);
    CHECK(ui.overlay == OV_GAME, "SELECT: game options");
    ui_draw(&ui, &g_s); shot("05-game-options-all");
    ui_input(&ui, B_DOWN); ui_input(&ui, B_CROSS);          /* second row */
    CHECK(info[cc].finished == now && !info[cc].playing, "Mark as completed: dated, out of Home");
    char st[4096]; text_of("ms0/SHELF/games.txt", st, sizeof(st));
    CHECK(strstr(st, "Crisis Core - Final Fantasy VII (EU) [Undub v1.0].iso | 0 | ") != NULL, "kept in ms0:/SHELF/games.txt");
    ui_input(&ui, B_R);
    CHECK(ui.tab == TAB_FAV && ui.count[TAB_FAV] == 0, "R from All: Favourites, empty so far");
    ui_draw(&ui, &g_s); shot("05b-favourites-empty");
    ui_input(&ui, B_R);
    CHECK(ui.tab == 2 && ui.count[2] == 1, "R again: Finished lists it");
    ui_draw(&ui, &g_s); shot("06-finished");
    ui_input(&ui, B_R);                                  /* back to Playing */
    ui.sel[0] = 0;
    ui_input(&ui, B_SQUARE);
    CHECK(ui.overlay == OV_INFO && ui.njourney == 7 && ui.ov_sel == 0, "Square: Info opens on the Emblem page");
    ui_draw(&ui, &g_s); shot("07-info");
    ui_input(&ui, B_R);
    CHECK(ui.overlay == OV_INFO && ui.ov_sel == 1, "  ... R: the journey page");
    ui_draw(&ui, &g_s); shot("07b-info-journey");
    ui_input(&ui, B_DOWN); ui_draw(&ui, &g_s);
    CHECK(ui.ov_page == 0, "  ... DOWN with one page of entries stays on it");
    ui_input(&ui, B_L);
    CHECK(ui.ov_sel == 0, "  ... L: back to Emblem");
    ui_input(&ui, B_CIRCLE);
    ui_input(&ui, B_TRI);
    ui_draw(&ui, &g_s); shot("08-sort");
    ui_input(&ui, B_DOWN); ui_input(&ui, B_CROSS);
    CHECK(cfg.sort[0] == SORT_NAME && ui.overlay == OV_NONE, "sort by name");
    ui_input(&ui, B_TRI); ui_input(&ui, B_UP); ui_input(&ui, B_CROSS);
    ui.sel[0] = 0;
    for (int i = 0; i < ui.count[0]; i++) if (ui.list[0][i] == ys) ui.sel[0] = i;
    ui_input(&ui, B_SELECT);
    CHECK(ui.overlay == OV_GAME && ui.ov_sel == 0, "Options opens on its first row");
    /* "Start from...": first row for a game with a Sidequest slot */
    ui_draw(&ui, &g_s); shot("09a-options-start-from-row");
    ui_input(&ui, B_CROSS);
    CHECK(ui.overlay == OV_START && ui.start_n == 3 && ui.start_slot[0] == 0 && ui.start_slot[1] == 2 && ui.start_slot[2] == -1 && ui.ov_sel == 0,
          "Start from...: the slots that exist, newest first (suspend, state 2), then 'Start without loading'");
    ui_draw(&ui, &g_s); shot("09c-start-from");
    ui_input(&ui, B_DOWN);
    ui_draw(&ui, &g_s); shot("09d-start-from-state");
    CHECK(ui_input(&ui, B_CROSS) == ACT_LAUNCH && ui.act_game == ys && ui.act_from == 1 && ui.act_slot == 2 && ui.overlay == OV_NONE,
          "  ... X on state 2: the game is started with that slot asked for");
    ui_draw_launch(&ui, &g_s, ys); shot("16b-launch-from-state");
    sq_start_request(lib.g[ys].path, ui.act_slot);
    { char rq[400]; text_of("ms0/SIDEQUEST/START.REQ", rq, sizeof(rq));
      char want[400]; snprintf(want, sizeof(want), "%s\r\n2\r\n", lib.g[ys].path);
      FILE *o = fopen("ms0/SIDEQUEST/RESUME.REQ", "rb");
      CHECK(!strcmp(rq, want) && o == NULL, "  ... written as START.REQ (path, then the slot), and no RESUME.REQ beside it");
      if (o) fclose(o); }
    ui.act_from = 0;
    ui_input(&ui, B_SELECT); ui_input(&ui, B_CROSS);
    ui_input(&ui, B_UP);
    CHECK(ui.start_slot[ui.ov_sel] == -1, "  ... UP from the first row wraps to 'Start without loading'");
    ui_draw(&ui, &g_s); shot("09e-start-from-none");
    CHECK(ui_input(&ui, B_CROSS) == ACT_LAUNCH && ui.act_from == 2, "  ... X there: started with 'no slot, and no question' asked for");
    ui_draw_launch(&ui, &g_s, ys); shot("16c-launch-without-loading");
    sq_start_request(lib.g[ys].path, -1);
    { char rq[400], want[400]; text_of("ms0/SIDEQUEST/START.REQ", rq, sizeof(rq)); snprintf(want, sizeof(want), "%s\r\nnone\r\n", lib.g[ys].path);
      CHECK(!strcmp(rq, want), "  ... written as 'none'"); }
    sq_resume_request(NULL);
    { FILE *o = fopen("ms0/SIDEQUEST/START.REQ", "rb"); CHECK(o == NULL, "  ... and a request no game picked up is cleared at the next start of Shelf"); if (o) fclose(o); }
    ui.act_from = 0;
    ui_input(&ui, B_SELECT); ui_input(&ui, B_CROSS); ui_input(&ui, B_CIRCLE);
    CHECK(ui.overlay == OV_GAME && ui.ov_sel == 0, "  ... O goes back to Options");
    ui_input(&ui, B_DOWN);
    ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN);
    ui_draw(&ui, &g_s); shot("09-game-options");
    printf("  (game %d sel %d overlay %d ov_sel %d pic %d)\n", ui_selected_game(&ui), ys, ui.overlay, ui.ov_sel, info[ys].pic);
    ui_input(&ui, B_RIGHT);
    CHECK(info[ys].pic == 0, "Picture: RIGHT steps through what exists: suspend ...");
    ui_input(&ui, B_RIGHT);
    CHECK(info[ys].pic == 2, "  ... state 2 (1 is empty)");
    ui_input(&ui, B_RIGHT);
    CHECK(info[ys].pic == 6, "  ... then the icon");
    ui_input(&ui, B_RIGHT);
    CHECK(info[ys].pic == -1, "  ... then back to newest");
    ui_input(&ui, B_CIRCLE);

    /* favourites */
    ui_input(&ui, B_SELECT); ui_input(&ui, B_DOWN); ui_input(&ui, B_CROSS);         /* second row: the first is "Start from..." for this game */
    CHECK(info[ys].fav == 1 && ui.overlay == OV_NONE && ui.ncats == 7 && ui.count[TAB_FAV] == 1,
          "SELECT > Add to Favourites: it is on the Favourites tab (and there is no Favourites category any more)");
    text_of("ms0/SHELF/games.txt", st, sizeof(st));
    CHECK(strstr(st, "Felghana (USA) [Undub].iso | 0 | 0 | 0 | -1 | 1") != NULL, "  ... kept in games.txt as a sixth field");
    { FILE *o = fopen("ms0/SHELF/games.txt", "ab"); fprintf(o, "%s | 0 | 0 | 1 | -1\r\n", lib.g[te].path); fclose(o);
      ShInfo *i2 = calloc(lib.n, sizeof(ShInfo)); data_load_all(&lib, i2, now);
      CHECK(i2[ys].fav == 1 && i2[te].hidden == 1 && i2[te].fav == 0, "  ... read back; lines written by the first build (five fields) still read");
      free(i2); state_save(&lib, info); }
    { int cr = find(&lib, "Crimson"), zl = find(&lib, "ZeldaPSP");
      info[cr].fav = 1; info[zl].fav = 1; state_save(&lib, info); ui_rebuild(&ui); }
    ui_input(&ui, B_R); ui_input(&ui, B_R);
    CHECK(ui.tab == TAB_FAV && ui.count[TAB_FAV] == 3 && ui.fcat == 0, "Playing, R, R: the Favourites tab, three favourites, in All Games");
    ui_draw(&ui, &g_s); shot("04b-favourites");
    ui_input(&ui, B_TRI);
    printf("  favourites menu:"); for (int k = 0; k < ui.menu_n; k++) printf(" [%s %d]", ui.cat_name[ui.menu_cat[k]], ui.menu_cnt[k]); printf("\n");
    CHECK(ui.overlay == OV_CATS && ui.menu_n == 4 && ui.menu_cat[0] == 0 && ui.menu_cnt[0] == 3 &&
          !strcmp(ui.cat_name[ui.menu_cat[1]], "Other PSP Games") && ui.menu_cnt[1] == 1 &&
          !strcmp(ui.cat_name[ui.menu_cat[2]], "PORTS") && ui.menu_cnt[2] == 1 && !strcmp(ui.cat_name[ui.menu_cat[3]], "PSP") && ui.menu_cnt[3] == 1,
          "Triangle on Favourites: its own category menu, only categories that hold a favourite, with their counts");
    ui_draw(&ui, &g_s); shot("04c-favourites-menu");
    ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_CROSS);
    CHECK(ui.tab == TAB_FAV && !strcmp(ui.cat_name[ui.fcat], "PORTS") && ui.count[TAB_FAV] == 1 && ui.cat == 0,
          "  ... PORTS: the one favourite port; the All tab keeps its own category");
    { ShUi *u2 = calloc(1, sizeof(ShUi)); ShConfig c2 = cfg;
      u2->lib = &lib; u2->info = info; u2->cfg = &c2; u2->now = now; c2.open_tab = 0;
      ui_init(u2);
      CHECK(u2->fcat == 0 && u2->count[TAB_FAV] == 3, "  ... and at the next start Favourites is on All Games again");
      free(u2); ui_rebuild(&ui); }
    ui_input(&ui, B_TRI); ui_input(&ui, B_UP); ui_input(&ui, B_UP); ui_input(&ui, B_CROSS);     /* back to All Games */
    CHECK(ui.fcat == 0 && ui.count[TAB_FAV] == 3, "  ... and back to All Games from the menu");
    ui_input(&ui, B_L);
    CHECK(ui.tab == 1, "L from Favourites: All");
    /* pick one for me */
    ui_input(&ui, B_TRI);
    while (ui.ov_sel != 1) ui_input(&ui, B_UP);
    ui_draw(&ui, &g_s); shot("08b-menu-pick");
    ui_input(&ui, B_CROSS);
    { int pg = ui_selected_game(&ui);
      CHECK(ui.overlay == OV_NONE && pg >= 0 && !info[pg].playing && !info[pg].finished, "'Pick one for me' in the menu lands on a game not started yet"); }
    ui_input(&ui, B_TRI);
    CHECK(ui.overlay == OV_CATS && ui.ov_sel == 1, "the menu opens on 'Pick one for me' again, ready for another go");
    ui_input(&ui, B_CIRCLE);
    pick_cat(&ui, "PORTS"); ui_input(&ui, B_TRI);
    CHECK(ui.ov_sel == 2 + ui.cat, "  ... and on the category again once one has been chosen");
    ui_input(&ui, B_CIRCLE); pick_cat(&ui, "All Games");
    /* O no longer leaves; the PS button has a menu */
    CHECK(ui.tab == 1 && ui_input(&ui, B_CIRCLE) == ACT_REDRAW && ui.tab == 0 && ui.overlay == OV_NONE, "O on All, Favourites or Finished: back to Playing, and it no longer exits");
    CHECK(ui_input(&ui, B_CIRCLE) != ACT_XMB && ui.tab == 0, "  ... on Playing it does nothing");
    ui.up_mins = 254; ui.up_pct0 = 100; ui.up_pct = 53; strcpy(ui.clock, "13:24  BATT 53%");
    ui_input(&ui, B_HOME);
    CHECK(ui.overlay == OV_QUICK, "the PS button opens Shelf's own menu");
    ui_draw(&ui, &g_s); shot("14b-ps-menu");
    strcpy(ui.clock, "14:05  BATT 82%");
    CHECK(ui_input(&ui, B_CROSS) == ACT_USB && ui.usb_from_quick == 1 && ui.overlay == OV_NONE, "  ... USB connection, first row");
    ui.overlay = OV_USB; ui.usb_state = 1; ui.as_launcher = 0; ui_draw(&ui, &g_s); shot("14c-usb-as-app");
    ui.as_launcher = 1; ui_draw(&ui, &g_s); shot("14d-usb-as-launcher");
    ui.as_launcher = 0; ui.overlay = OV_NONE;
    ui_input(&ui, B_HOME); ui_input(&ui, B_UP);
    CHECK(ui_input(&ui, B_CROSS) == ACT_XMB, "  ... Exit to XMB is its last row (UP from the top reaches it)");
    ui_input(&ui, B_HOME); ui_input(&ui, B_DOWN);
    CHECK(ui_input(&ui, B_CROSS) == ACT_REDRAW && ui.overlay == OV_SETTINGS, "  ... Settings opens the settings");
    ui_input(&ui, B_CIRCLE);
    ui_input(&ui, B_HOME); ui_input(&ui, B_HOME);
    CHECK(ui.overlay == OV_NONE, "  ... the PS button again closes it");
    /* where you were */
    ui_input(&ui, B_R); pick_cat(&ui, "PSP");
    ui_input(&ui, B_RIGHT); ui_input(&ui, B_RIGHT);
    { int was = ui_selected_game(&ui), wcat = ui.cat;
      ui_remember(&ui); config_save(&cfg);
      ShConfig c2; config_load(&c2);
      ShUi *u2 = calloc(1, sizeof(ShUi));
      u2->lib = &lib; u2->info = info; u2->cfg = &c2; u2->now = now;
      ui_init(u2);
      CHECK(u2->tab == 1 && u2->cat == wcat && ui_selected_game(u2) == was && !strcmp(c2.last_cat, "PSP"),
            "after a game, Shelf opens where you were: same tab, category and game");
      free(u2); ui_rebuild(&ui); }
    pick_cat(&ui, "All Games"); ui_input(&ui, B_L);
    ui.sel[0] = 0;

    /* settings */
    ui_input(&ui, B_START);
    CHECK(ui.overlay == OV_SETTINGS && ui.ark && ui.nplugins == 6 && ui.set_focus == 0, "SELECT: settings, on the list of sections; ARK's SETTINGS.TXT and 6 plugin lines read");
    ui_draw(&ui, &g_s); shot("10-settings-sections");
    ui_input(&ui, B_RIGHT);
    CHECK(cfg.open_tab == 3 && ui.set_focus == 1, "on the sections, RIGHT (or X) goes into the section; nothing is changed by it");
    ui_input(&ui, B_CIRCLE);
    CHECK(ui.overlay == OV_SETTINGS && ui.set_focus == 0, "O inside a section goes back to the sections");
    ui_input(&ui, B_CROSS);
    ui_draw(&ui, &g_s); shot("10-settings-shelf");
    ui_input(&ui, B_DOWN);
    CHECK(!strcmp(ui.items[ui.ov_sel].label, SH_TAB_ALL " opens on") && !strcmp(ui.items[ui.ov_sel].value, "All Games"), "second Shelf setting: the category the tab opens on");
    ui_input(&ui, B_RIGHT);
    CHECK(!strcmp(cfg.all_default, "MINIS") && ui.cat == 1, "  ... RIGHT steps through the categories (MINIS next), and All shows it at once");
    ui_input(&ui, B_LEFT);
    CHECK(!strcmp(cfg.all_default, "All Games"), "  ... LEFT back");
    ui_input(&ui, B_DOWN);
    CHECK(!strcmp(ui.items[ui.ov_sel].label, "Favourites open on"), "third: the category Favourites open on");
    ui_input(&ui, B_DOWN);
    CHECK(!strcmp(ui.items[ui.ov_sel].label, SH_TAB_ALL ": games per row"), "fourth Shelf setting is the tab's games per row");
    ui_input(&ui, B_RIGHT);
    CHECK(cfg.grid_cols == 3, "RIGHT from 6 wraps to 3 per row");
    ui_input(&ui, B_RIGHT); ui_input(&ui, B_RIGHT);
    CHECK(cfg.grid_cols == 5, "RIGHT: 4, then 5");
    ui_draw(&ui, &g_s); shot("10-settings-grid");
    config_save(&cfg);
    { ShConfig c2; config_load(&c2); CHECK(c2.grid_cols == 5, "games per row saved in config.txt"); }
    ui_input(&ui, B_LEFT); ui_input(&ui, B_LEFT); ui_input(&ui, B_LEFT);
    CHECK(cfg.grid_cols == 6, "LEFT back to 6");
    ui_input(&ui, B_CIRCLE);
    ui.ms_free_mb = 3277; ui.ms_total_mb = 30436;
    ui_input(&ui, B_DOWN);
    CHECK(ui.set_sec == 1 && !strcmp(ui.items[0].label, "Rescan library") && !strcmp(ui.items[3].value, "4 of 12  (1 completed)") && !strcmp(ui.items[4].value, "79h 05m") &&
          !strcmp(ui.items[5].value, "7 / 92") && !strcmp(ui.items[7].value, "3.2 GB free of 29.7 GB"), "DOWN on the sections: Library, shown beside it (rescan, totals)");
    ui_draw(&ui, &g_s); shot("10c-settings-library");
    ui_input(&ui, B_DOWN);
    ui_draw(&ui, &g_s); shot("11-settings-ark-preview");
    ui_input(&ui, B_CROSS);
    ui_draw(&ui, &g_s); shot("11-settings-ark");
    CHECK(ark_get(ui.ark, ARK_CLOCK_GAME) == 3 && ark_get(ui.ark, ARK_HIGHMEM) == 0 && ark_get(ui.ark, ARK_MSCACHE) == 1 &&
          ark_get(ui.ark, ARK_ISOCACHE) == 1 && ark_get(ui.ark, ARK_USBCHARGE) == 1, "ARK values read: 333 (overclock), extra memory off, caches on");
    ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN);     /* extra memory */
    ui_input(&ui, B_RIGHT);
    CHECK(ark_get(ui.ark, ARK_HIGHMEM) == 1, "extra memory changed to On");
    ui_input(&ui, B_LEFT);
    ui_input(&ui, B_UP); ui_input(&ui, B_UP);
    ui_input(&ui, B_RIGHT);                           /* game clock 333 -> 383 */
    ui_input(&ui, B_CIRCLE); ui_input(&ui, B_DOWN); ui_input(&ui, B_CROSS);
    ui_draw(&ui, &g_s); shot("12-settings-plugins");
    ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_CROSS);   /* scanline off */
    ui_input(&ui, B_CIRCLE); ui_input(&ui, B_DOWN); ui_input(&ui, B_CROSS);
    ui_draw(&ui, &g_s); shot("13-settings-power");
    ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_CROSS);
    CHECK(ui.overlay == OV_CONFIRM, "Restart asks first");
    ui_draw(&ui, &g_s); shot("14-confirm");
    CHECK(ui_input(&ui, B_CROSS) == ACT_RESTART, "  ... X restarts");
    ui_input(&ui, B_CIRCLE);                         /* back to the sections */
    CHECK(ui.overlay == OV_SETTINGS && ui.set_focus == 0, "(O: sections)");
    ui_input(&ui, B_CIRCLE);                         /* leave the settings: files written */
    text_of("ms0/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT", st, sizeof(st));
    printf("----- SETTINGS.TXT after -----\n%s-----\n", st);
    CHECK(strstr(st, "game, cpuclock:383, on") && strstr(st, "vsh, cpuclock:333, on") && !strstr(st, "overclock") &&
          strstr(st, "always, highmem, off") && strstr(st, "ms0:/PSP/GAME/PORTS/MajoraPSP/EBOOT.PBP, highmem, on") &&
          strstr(st, "ULUS10558, cpuclock:222, on") && strstr(st, "# ARK settings"),
          "SETTINGS.TXT: CPU speed split game/XMB, unchanged settings and per-game lines kept");
    text_of("ms0/SEPLUGINS/PLUGINS.TXT", st, sizeof(st));
    CHECK(strstr(st, "game, ms0:/SEPLUGINS/scanline.prx, off") && strstr(st, "game, ms0:/SEPLUGINS/sidequest.prx, on") && strstr(st, "# a comment") &&
          strstr(st, "MajoraPSP/EBOOT.PBP, ms0:/SEPLUGINS/sidequest.prx, off"), "PLUGINS.TXT: only Scanline's line changed");
    CHECK(ui.overlay == OV_CONFIRM, "leaving after ARK changes offers a restart");
    ui_draw(&ui, &g_s); shot("15-restart-offer");
    ui_input(&ui, B_CIRCLE);

    /* launcher install / removal */
    char m[64];
    CHECK(!launcher_installed(ui.ark_dir), "Shelf is not the launcher yet (psp5 is)");
    CHECK(launcher_install(ui.ark_dir, ui.self, m, sizeof(m)) == 0 && launcher_installed(ui.ark_dir), "install: Shelf becomes VBOOT.PBP");
    text_of("ms0/PSP/SAVEDATA/ARK_01234/VBOOT_BEFORE_SHELF.PBP", st, sizeof(st));
    CHECK(memmem(st, 400, "psp5", 4) != NULL, "  ... the old launcher (psp5) is kept as VBOOT_BEFORE_SHELF.PBP");
    launcher_pause(ui.ark_dir);
    text_of("ms0/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT", st, sizeof(st));
    CHECK(strstr(st, "always, launcher, off") != NULL, "exit to XMB from launcher mode turns the mode off ...");
    CHECK(launcher_resume_if_paused(ui.ark_dir) == 1, "  ... and on again the next time Shelf starts");
    CHECK(launcher_update(ui.ark_dir, ui.self) == 0, "the start-up copy is left alone while Shelf's EBOOT is the one it was made from");
    { FILE *o = fopen("ms0/PSP/GAME/SHELF/EBOOT.PBP", "ab"); fputs("a newer build", o); fclose(o);
      CHECK(launcher_update(ui.ark_dir, ui.self) == 1 && launcher_installed(ui.ark_dir), "a newer Shelf on the stick replaces the start-up copy by itself");
      char a1[1 << 16], a2[1 << 16]; text_of("ms0/PSP/GAME/SHELF/EBOOT.PBP", a1, sizeof(a1)); text_of("ms0/PSP/SAVEDATA/ARK_01234/VBOOT.PBP", a2, sizeof(a2));
      FILE *x = fopen("ms0/PSP/GAME/SHELF/EBOOT.PBP", "rb"), *z = fopen("ms0/PSP/SAVEDATA/ARK_01234/VBOOT.PBP", "rb");
      fseek(x, 0, SEEK_END); fseek(z, 0, SEEK_END);
      CHECK(ftell(x) == ftell(z) && launcher_update(ui.ark_dir, ui.self) == 0, "  ... once"); fclose(x); fclose(z); (void)a1; (void)a2; }
    CHECK(launcher_update(ui.ark_dir, "ms0:/PSP/SAVEDATA/ARK_01234/VBOOT.PBP") == 0, "  ... and never when Shelf is running as that copy");
    CHECK(launcher_remove(ui.ark_dir, m, sizeof(m)) == 0 && launcher_installed(ui.ark_dir), "start at power on, off: Shelf stays ARK's launcher file");
    text_of("ms0/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT", st, sizeof(st));
    CHECK(strstr(st, "always, launcher, off") != NULL && !strstr(st, "always, launcher, on"), "  ... and ARK's launcher setting is off: the PSP powers on into the XMB");

    /* turning start-at-power-on on with ARK changes not yet saved: both must end up in the file */
    ui_input(&ui, B_START);
    ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_CROSS);
    ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_DOWN); ui_input(&ui, B_LEFT);    /* USB charging: on -> off */
    CHECK(!strcmp(ui.items[ui.ov_sel].label, "USB charging") && ark_get(ui.ark, ARK_USBCHARGE) == 0, "(USB charging switched off, not saved yet)");
    ui_input(&ui, B_CIRCLE); ui_input(&ui, B_UP); ui_input(&ui, B_UP); ui_input(&ui, B_CROSS);
    for (int i = 0; i < 10; i++) ui_input(&ui, B_DOWN);
    CHECK(!strcmp(ui.items[ui.ov_sel].label, "Start Shelf at power on"), "(on the start-at-power-on row)");
    ui_input(&ui, B_CROSS); ui_input(&ui, B_CROSS);
    ui_draw(&ui, &g_s); shot("15b-power-on-set");
    ui_input(&ui, B_CROSS);
    CHECK(ui.overlay == OV_SETTINGS && ui.launcher_on, "start at power on set from the settings screen");
    ui_input(&ui, B_CIRCLE); ui_input(&ui, B_CIRCLE);
    text_of("ms0/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT", st, sizeof(st));
    CHECK(strstr(st, "always, launcher, on") && strstr(st, "always, usbcharge, off") && ui.overlay == OV_CONFIRM,
          "  ... the unsaved ARK change and the launcher line are both in SETTINGS.TXT; a restart is offered");
    ui_input(&ui, B_CIRCLE);
    launcher_remove(ui.ark_dir, m, sizeof(m));

    /* other screens */
    ui_draw_launch(&ui, &g_s, ys); shot("16-launch");
    ui_draw_launch(&ui, &g_s, zel); shot("17-launch-homebrew");
    ui_draw_progress(&ui, &g_s, "Finding your games", 37, 112, "ms0:/ISO/PSP/Ys Seven (USA) [Undub].iso"); shot("18-progress");
    ui.overlay = OV_USB; ui.usb_state = 2; ui_draw(&ui, &g_s); shot("19-usb"); ui.overlay = OV_NONE;
    /* the info screen for a game never played */
    ui.tab = 1; ui.cat = 0;
    ui_rebuild(&ui);
    int cg = find(&lib, "Crimson");
    for (int i = 0; i < ui.count[1]; i++) if (ui.list[1][i] == cg) ui.sel[1] = i;
    ui_input(&ui, B_SQUARE);
    ui_draw(&ui, &g_s); shot("20-info-new");
    ui_input(&ui, B_CIRCLE);
    /* Japanese journey text drawn with the Japanese font */
    { int jg = -1; for (int i = 0; i < lib.n; i++) if (!strcmp(lib.g[i].id, "NPJH50342")) jg = i;
      CHECK(jg >= 0, "the Japanese-journey game is in the library");
      for (int i = 0; i < ui.count[1]; i++) if (ui.list[1][i] == jg) ui.sel[1] = i;
      ui_input(&ui, B_SQUARE);
      ui_draw(&ui, &g_s); shot("21-info-japanese"); }

    /* back to Shelf after a game, with Shelf opened from the XMB and not set to start at power on */
    { char mm[64], sp[160]; ShText *t = malloc(sizeof(ShText));
      launcher_remove(ui.ark_dir, mm, sizeof(mm));
      sh_snprintf(sp, sizeof(sp), "%sSETTINGS.TXT", ui.ark_dir);
      /* a stick as it was before Shelf: another launcher (psp5) is VBOOT.PBP and ARK's launcher setting is on */
      remove("ms0/PSP/SAVEDATA/ARK_01234/VBOOT.PBP");
      rename("ms0/PSP/SAVEDATA/ARK_01234/VBOOT_BEFORE_SHELF.PBP", "ms0/PSP/SAVEDATA/ARK_01234/VBOOT.PBP");
      tf_load(t, sp); ark_set(t, ARK_LAUNCHER, 1); tf_save(t);
      CHECK(!launcher_installed(ui.ark_dir) && launcher_takeover(ui.ark_dir, ui.self) == 1 && launcher_installed(ui.ark_dir),
            "ARK's launcher is on with another launcher: Shelf takes its place when it starts");
      tf_load(t, sp);
      CHECK(ark_get(t, ARK_LAUNCHER) == 1 && launcher_takeover(ui.ark_dir, ui.self) == 0, "  ... the setting stays on; nothing more to do the next time");
      /* and one where the launcher setting is off, with the other launcher still in place */
      remove("ms0/PSP/SAVEDATA/ARK_01234/VBOOT.PBP");
      rename("ms0/PSP/SAVEDATA/ARK_01234/VBOOT_BEFORE_SHELF.PBP", "ms0/PSP/SAVEDATA/ARK_01234/VBOOT.PBP");
      tf_load(t, sp); ark_set(t, ARK_LAUNCHER, 0); tf_save(t);
      CHECK(!launcher_installed(ui.ark_dir) && launcher_takeover(ui.ark_dir, ui.self) == 0, "(launcher setting off: nothing is touched until a game is started)");
      CHECK(launcher_return_arm(ui.ark_dir, ui.self) == 1 && launcher_installed(ui.ark_dir), "starting a game: Shelf is put in as ARK's launcher for the way back");
      tf_load(t, sp);
      CHECK(ark_get(t, ARK_LAUNCHER) == 1, "  ... and the launcher setting is on while the game runs");
      CHECK(launcher_return_done(ui.ark_dir) == 1, "Shelf starts again after the game and sees why");
      tf_load(t, sp);
      CHECK(ark_get(t, ARK_LAUNCHER) == 0 && launcher_return_done(ui.ark_dir) == 0, "  ... the setting is off again at once: power-on still goes to the XMB");
      CHECK(launcher_return_arm(ui.ark_dir, "ms0:/PSP/SAVEDATA/ARK_01234/VBOOT.PBP") == 1 && launcher_return_done(ui.ark_dir) == 1,
            "  ... and the next game started from that Shelf comes back the same way");
      ark_set(t, ARK_LAUNCHER, 1); tf_save(t);
      CHECK(launcher_return_arm(ui.ark_dir, ui.self) == 0 && launcher_return_done(ui.ark_dir) == 0, "with 'Start Shelf at power on' on there is nothing to arm");
      ark_set(t, ARK_LAUNCHER, 0); tf_save(t);
      /* the setting, switched off in the settings screen, puts the old launcher back */
      ui.overlay = OV_NONE;                       /* an earlier check left the journey open */
      ui_input(&ui, B_START); ui_input(&ui, B_CROSS);
      for (int i = 0; i < 11; i++) ui_input(&ui, B_DOWN);
      CHECK(!strcmp(ui.items[ui.ov_sel].label, "After a game") && !strcmp(ui.items[ui.ov_sel].value, "Back to Shelf"), "Settings > Shelf > After a game: Back to Shelf");
      ui_draw(&ui, &g_s); shot("10d-settings-after-a-game");
      ui_input(&ui, B_RIGHT);
      text_of("ms0/PSP/SAVEDATA/ARK_01234/VBOOT.PBP", st, sizeof(st));
      CHECK(cfg.return_to_shelf == 0 && launcher_installed(ui.ark_dir), "  ... set to XMB: only the setting changes, Shelf stays in place");
      ui_input(&ui, B_LEFT); ui_input(&ui, B_CIRCLE); ui_input(&ui, B_CIRCLE);
      if (ui.overlay == OV_CONFIRM) ui_input(&ui, B_CIRCLE);
      free(t); }
    /* opened from the Game menu, Shelf hands over to its launcher copy */
    { char vb[160];
      CHECK(launcher_ready(ui.ark_dir, ui.self, vb, sizeof(vb)) == 1 && !strcmp(vb, "ms0:/PSP/SAVEDATA/ARK_01234/VBOOT.PBP") && launcher_installed(ui.ark_dir),
            "from the Game menu: the launcher copy is ready (VBOOT.PBP is Shelf) and its path is given");
      CHECK(launcher_ready(ui.ark_dir, vb, vb, sizeof(vb)) == 0, "  ... the launcher copy itself does not hand over again");
      remove("ms0/PSP/SAVEDATA/ARK_01234/VBOOT.PBP");
      CHECK(launcher_ready(ui.ark_dir, ui.self, vb, sizeof(vb)) == 1 && launcher_installed(ui.ark_dir), "  ... a missing VBOOT.PBP is put back first");
      CHECK(launcher_ready(ui.ark_dir, "ms0:/PSP/GAME/NOPE/EBOOT.PBP", vb, sizeof(vb)) == 1, "  ... and when it is already Shelf nothing needs copying"); }

    /* Shelf starts no longer write to PLUGINS.TXT (it once added "off" lines for another plugin) */
    { char before[4096]; text_of("ms0/SEPLUGINS/PLUGINS.TXT", before, sizeof(before));
      ShUi *u2 = calloc(1, sizeof(ShUi)); ShConfig c2 = cfg;
      u2->lib = &lib; u2->info = info; u2->cfg = &c2; u2->now = now; ui_init(u2); free(u2); ui_rebuild(&ui);
      text_of("ms0/SEPLUGINS/PLUGINS.TXT", st, sizeof(st));
      CHECK(!strcmp(st, before) && strstr(st, "OtherHud.prx, on") && !strstr(st, "OtherHud.prx, off"), "PLUGINS.TXT is left exactly as it was: no lines added for OtherHud"); }

    /* time and battery since the PSP was powered on */
    { ShUptime up; char ln[160];
      remove("ms0/SHELF/uptime.txt"); remove("ms0/SHELF/battery.log");
      uptime_begin(&up, 1000, 100);
      CHECK(up.start == 1000 && up.start_pct == 100, "first start: a new power-on begins, battery noted");
      uptime_mark(&up, 1030, 91, 1);                       /* a game is started */
      uptime_begin(&up, 1150, 70);                         /* Shelf comes back two hours later */
      CHECK(up.start == 1000 && up.start_pct == 100 && up.seen == 1150, "back from a game: the same power-on carries on, the game's time included");
      uptime_mark(&up, 1160, 68, 0);                       /* shut down from Shelf, or the 5-minute note before a power-off */
      uptime_begin(&up, 5000, 97);
      text_of("ms0/SHELF/battery.log", st, sizeof(st));
      uptime_line(1000, 100, 1160, 68, ln, sizeof(ln));
      printf("  log: %s", st);
      CHECK(up.start == 5000 && up.start_pct == 97 && strstr(st, ln) && strstr(st, "2h 40m") && strstr(st, "battery 100% to 68% | -32%"),
            "after a power-off: a new power-on begins, and the last one is one line in battery.log");
      uptime_mark(&up, 5010, 96, 1);
      uptime_begin(&up, 5010 + 13 * 60, 40);
      CHECK(up.start == 5010 + 13 * 60, "more than 12 hours without Shelf: counted as a new power-on"); }

    /* with the battery plugin (Runtime): it keeps the record in every program, in a folder of its own; Shelf only reads it */
    { ShUptime up; GaRec r; char f[96], e[200], own[96];
      system("mkdir -p ms0/RUNTIME");
      remove("ms0/RUNTIME/uptime.txt");
      uptime_begin(&up, 8990, 100);                                                /* Shelf's own record, from before */
      text_of("ms0/SHELF/uptime.txt", own, sizeof(own));
      CHECK(up.plugin == 0 && up.bad == 0, "no file in ms0:/RUNTIME: no Runtime, Shelf keeps its own count and shows it");
      ga_parse(NULL, &r); ga_step(&r, 9000, 100, 1, e, sizeof(e));                 /* power on */
      ga_format(&r, f, sizeof(f)); put_text("ms0/RUNTIME/uptime.txt", f);
      for (u32 m = 9002; m <= 9120; m += 2) { ga_parse(f, &r); ga_step(&r, m, 90, 0, e, sizeof(e)); ga_format(&r, f, sizeof(f)); }   /* two hours in a game */
      ga_parse(f, &r); ga_step(&r, 9120 + 240, 85, 0, e, sizeof(e)); ga_format(&r, f, sizeof(f));                                    /* four hours asleep */
      put_text("ms0/RUNTIME/uptime.txt", f);
      uptime_begin(&up, 9361, 85);
      text_of("ms0/RUNTIME/uptime.txt", st, sizeof(st));
      CHECK(up.plugin == 1 && up.bad == 0 && up.start == 9000 && up.start_pct == 100 && up.asleep == 240 && !strcmp(st, f),
            "Runtime's record in ms0:/RUNTIME, fresh: Shelf shows it (start, battery then, sleep) and does not write the file");
      uptime_mark(&up, 9362, 84, 1);
      text_of("ms0/RUNTIME/uptime.txt", st, sizeof(st));
      { char now_own[96]; text_of("ms0/SHELF/uptime.txt", now_own, sizeof(now_own));
        CHECK(!strcmp(st, f) && !strcmp(now_own, own), "  ... not when a game is started either, and nothing in ms0:/SHELF is written meanwhile"); }
      ui.up_hide = 0; ui.up_mins = 9361 - 9000 - 240; ui.up_sleep = 240; ui.up_pct0 = 100; ui.up_pct = 85; strcpy(ui.clock, "13:24  BATT 85%");
      ui_input(&ui, B_HOME); ui_draw(&ui, &g_s); shot("14e-ps-menu-with-runtime"); ui_input(&ui, B_CIRCLE);
      ui.up_sleep = 0;
      CHECK(uptime_refresh(&up, 9600) == 0 && up.plugin == 0 && up.bad == 0, "Runtime's record gone stale (plugin removed): Shelf keeps its own count again");
      uptime_begin(&up, 9600, 80);
      CHECK(up.start == 9600 && up.plugin == 0 && up.bad == 0, "  ... starting with a new power-on");
      put_text("ms0/RUNTIME/uptime.txt", "junk 2 2 2 2 2");
      CHECK(uptime_refresh(&up, 9600) == 0 && up.bad == 1, "a record that does not read as one is not followed, and is marked: nothing is shown");
      { FILE *o = fopen("ms0/RUNTIME/uptime.txt", "wb"); fclose(o); }
      CHECK(uptime_refresh(&up, 9600) == 0 && up.bad == 1, "  ... the same for an empty file (what a damaged folder leaves)");
      { FILE *o = fopen("ms0/RUNTIME/uptime.txt", "wb"); fwrite("\0PBP\0\0\1\0(\0\0\0`\1\0\0", 1, 16, o); fclose(o); }
      CHECK(uptime_refresh(&up, 9600) == 0 && up.bad == 1, "  ... and for a file holding part of another file");
      put_text("ms0/RUNTIME/uptime.txt", "9000 100 9598 85 1 0\r\n");
      CHECK(uptime_refresh(&up, 9600) == 0 && up.bad == 1, "  ... and for a record not written by Runtime");
      up.bad = 0; uptime_mark(&up, 9600, 80, 1);           /* Shelf had handed over to a game in this power-on */
      uptime_begin(&up, 9601, 79);
      text_of("ms0/SHELF/uptime.txt", st, sizeof(st));
      CHECK(up.bad == 1 && up.plugin == 0 && up.start == 9600 && strstr(st, "9601"), "Shelf's own count goes on underneath while nothing is shown");
      ui.up_hide = 1;
      ui_input(&ui, B_HOME); ui_draw(&ui, &g_s); shot("14f-ps-menu-runtime-unreadable"); ui_input(&ui, B_CIRCLE);
      ui.up_hide = 0;
      remove("ms0/RUNTIME/uptime.txt");
      CHECK(uptime_refresh(&up, 9602) == 0 && up.bad == 0, "the file gone again: shown again, from Shelf's own count"); }

    /* Home, Favourites and Finished can each be switched off */
    { ui.overlay = OV_NONE; ui.tab = 0;
      CHECK(cfg.tab_home && cfg.tab_fav && cfg.tab_finished, "as shipped: all four tabs show");
      ui_input(&ui, B_START); ui_input(&ui, B_CROSS);
      while (strcmp(ui.items[ui.ov_sel].label, "Home tab")) ui_input(&ui, B_DOWN);
      ui_draw(&ui, &g_s); shot("10e-settings-tabs");
      cfg.open_tab = 0;
      ui_input(&ui, B_RIGHT);
      CHECK(cfg.tab_home == 0 && ui.tab == 1 && cfg.open_tab == 1, "Settings > Shelf > Home tab: Off. You were on Home, so you are on All; 'Open on' moves to All too");
      ui_input(&ui, B_CIRCLE); ui_input(&ui, B_CIRCLE);
      ui_draw(&ui, &g_s); shot("03b-all-without-home");
      CHECK(ui.overlay == OV_NONE && ui_input(&ui, B_CIRCLE) == ACT_REDRAW && ui.tab == 1, "  ... O on All does nothing now");
      ui_input(&ui, B_L);
      CHECK(ui.tab == 2, "  ... L from All skips Home: Finished");
      ui_input(&ui, B_R); ui_input(&ui, B_R);
      CHECK(ui.tab == TAB_FAV, "  ... R, R: All, Favourites");
      ui_input(&ui, B_CIRCLE);
      CHECK(ui.tab == 1, "  ... O on Favourites goes back to All");
      ui_input(&ui, B_SELECT);
      ui_draw(&ui, &g_s); shot("09b-options-without-home"); ui_input(&ui, B_CIRCLE);
      cfg.tab_fav = 0; cfg.tab_finished = 0;
      ui_input(&ui, B_R);
      CHECK(ui.tab == 1, "all three off: L and R stay on All");
      ui_draw(&ui, &g_s); shot("03c-all-only");
      { ShUi *u2 = calloc(1, sizeof(ShUi)); ShConfig c2 = cfg;
        u2->lib = &lib; u2->info = info; u2->cfg = &c2; u2->now = now; c2.open_tab = 3; c2.last_tab = 2;
        ui_init(u2);
        CHECK(u2->tab == 1, "  ... and a start that would land on a tab that is off lands on All");
        free(u2); ui_rebuild(&ui); }
      config_save(&cfg); { ShConfig c3; config_load(&c3); CHECK(!c3.tab_home && !c3.tab_fav && !c3.tab_finished, "  ... kept in config.txt"); }
      cfg.tab_home = cfg.tab_fav = cfg.tab_finished = 1; cfg.open_tab = 3; config_save(&cfg); ui.tab = 0; }

    /* the note for Sidequest: resume without asking */
    { sq_resume_request(lib.g[ys].path);
      text_of("ms0/SIDEQUEST/RESUME.REQ", st, sizeof(st));
      char want[256]; snprintf(want, sizeof(want), "%s\r\n", lib.g[ys].path);
      CHECK(!strcmp(st, want), "started from Playing: RESUME.REQ holds the game's path for Sidequest");
      sq_resume_request(NULL);
      FILE *o = fopen("ms0/SIDEQUEST/RESUME.REQ", "rb");
      CHECK(o == NULL, "started from All (or Shelf starting again): no request is left waiting");
      if (o) fclose(o); }

    /* the EBOOT built for the PSP: its own icon and background, read the way any game's are */
    { ShGame me; memset(&me, 0, sizeof(me));
      FILE *e = fopen("ms0/BUILT/EBOOT.PBP", "rb");
      if (!e) printf("SKIP the built EBOOT.PBP is not there (run make first)\n");
      else {
        fclose(e);
        int r = scan_read_game(&me, "ms0:/BUILT/EBOOT.PBP", 1);
        CHECK(r == 0 && me.type == GT_HOMEBREW && !strcmp(me.title, "Shelf"), "the built EBOOT.PBP is a homebrew titled Shelf");
        static u16 ic[ICON_W * ICON_H], sm[SMALL_W * SMALL_H], bk[PIC_W * PIC_H];
        CHECK(me.has_icon && me.has_pic && cache_read(me.key, PIC_ICON, ic) == 0 && cache_read(me.key, PIC_SMALL, sm) == 0 &&
              cache_read(me.key, PIC_BACK, bk) == 0, "  ... its ICON0 and PIC1 decode and cache");
        CHECK(launcher_installed("ms0:/BUILT/") == 0 && !rename("ms0/BUILT/EBOOT.PBP", "ms0/BUILT/VBOOT.PBP") && launcher_installed("ms0:/BUILT/") == 1,
              "  ... and as VBOOT.PBP it is recognised as Shelf (start-at-power-on check)");
        rename("ms0/BUILT/VBOOT.PBP", "ms0/BUILT/EBOOT.PBP");
        gfx_rect(&g_s, 0, 0, SCR_W, SCR_H, C_BG);
        gfx_blit565_scaled(&g_s, 0, 0, SCR_W, SCR_H, bk, PIC_W, PIC_H);
        gfx_dim(&g_s, 0, 0, SCR_W, 96, 45);
        gfx_text(&g_s, 12, 8, "ICON0 and PIC1 read back out of the built EBOOT.PBP", C_WHITE);
        gfx_blit565(&g_s, 12, 24, ic, ICON_W, ICON_H); gfx_text(&g_s, 12, 108, "144x80 (XMB, 3 per row)", C_GREY);
        gfx_blit565(&g_s, 172, 24, sm, SMALL_W, SMALL_H); gfx_text(&g_s, 172, 68, "72x40 (6 per row)", C_GREY);
        shot("22-icon-check");
      } }

    printf("\n%d failure(s)\n", fails);
    return fails ? 1 : 0;
}
