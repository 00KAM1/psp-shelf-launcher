/*
 * Shelf - the screens. Layout as agreed in render 2 (claude/shelf-render-2.md).
 * Draws onto a surface and turns button presses into changes or actions;
 * the main loop (app.c) does what only a PSP can do (start a game, USB, power).
 * No PSP calls here apart from sceIo*, so the PC renders use this same code.
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ui.h"
#include "image.h"

#define CARD_W 86
#define CARD_H 49

/* ---- pictures, loaded when first needed ------------------------------------ */
static u16 *g_pack;                      /* every 72x40 icon, from CACHE/SMALL.BIN */
static u16 *g_small[SH_MAX_GAMES];       /* 72x40 icons read one by one, if that file is missing */
static u8   g_small_state[SH_MAX_GAMES]; /* 0 not tried, 1 have, 2 none */
static u16 *g_card[SH_MAX_GAMES];        /* 86x49 Playing cards */
static s8   g_card_src[SH_MAX_GAMES];    /* which picture the card was made from, -2 = none yet */
static u16  g_hero[PIC_W * PIC_H];
static int  g_hero_game = -1, g_hero_src = -9;
static u16  g_icon[ICON_W * ICON_H];
static u16 *g_grid[SH_MAX_GAMES];        /* All-tab icons at the 3/4/5-per-row size */
static u8   g_grid_state[SH_MAX_GAMES];  /* 0 not tried, 1 have, 2 none */
static int  g_grid_w, g_grid_n;          /* the size they were made at; how many are held */
static int  g_icon_game = -1, g_icon_ok;

static void forget_pictures(void)
{
    for (int i = 0; i < SH_MAX_GAMES; i++) {
        free(g_small[i]); g_small[i] = NULL; g_small_state[i] = 0;
        free(g_card[i]); g_card[i] = NULL; g_card_src[i] = -2;
        free(g_grid[i]); g_grid[i] = NULL; g_grid_state[i] = 0;
    }
    g_grid_n = 0;
    g_hero_game = -1; g_icon_game = -1;
}

static const u16 *small_icon(ShUi *ui, int g)
{
    if (g < 0 || g >= SH_MAX_GAMES) return NULL;
    if (g_pack) return ui->lib->g[g].has_icon ? g_pack + g * (SMALL_W * SMALL_H) : NULL;
    if (!g_small_state[g]) {
        g_small_state[g] = 2;
        if (ui->lib->g[g].has_icon) {
            u16 *b = malloc(SMALL_W * SMALL_H * 2);
            if (b && cache_read(ui->lib->g[g].key, PIC_SMALL, b) == 0) { g_small[g] = b; g_small_state[g] = 1; }
            else free(b);
        }
    }
    return g_small[g];
}

static const u16 *big_icon(ShUi *ui, int g)
{
    if (g_icon_game != g) {
        g_icon_game = g;
        g_icon_ok = ui->lib->g[g].has_icon && cache_read(ui->lib->g[g].key, PIC_ICON, g_icon) == 0;
    }
    return g_icon_ok ? g_icon : NULL;
}

/* Which Sidequest picture a game shows: 0..5, or -1 for its icon. */
static int pic_source(ShUi *ui, int g)
{
    ShInfo *in = &ui->info[g];
    if (!in->sq_dir[0]) return -1;
    if (in->pic == 6) return -1;
    if (in->pic >= 0 && in->pic <= 5) return in->sq_has[(int)in->pic] ? in->pic : -1;
    if (ui->cfg->pic_mode == 1) return -1;
    return in->sq_newest;
}

/* PIC1 darkened with the icon on top (or the icon on dark), 240x136. */
static void compose_icon_pic(ShUi *ui, int g, u16 *out)
{
    if (!(ui->lib->g[g].has_pic && cache_read(ui->lib->g[g].key, PIC_BACK, out) == 0))
        for (int i = 0; i < PIC_W * PIC_H; i++) out[i] = 0x1062;
    else
        for (int i = 0; i < PIC_W * PIC_H; i++) {          /* to 55% */
            u16 p = out[i];
            u32 r = (p & 31) * 55 / 100, gg = ((p >> 5) & 63) * 55 / 100, b = (p >> 11) * 55 / 100;
            out[i] = (u16)(r | gg << 5 | b << 11);
        }
    const u16 *ic = big_icon(ui, g);
    if (!ic) return;
    int ox = (PIC_W - ICON_W) / 2, oy = (PIC_H - ICON_H) / 2;
    for (int y = 0; y < ICON_H; y++) memcpy(out + (oy + y) * PIC_W + ox, ic + y * ICON_W, ICON_W * 2);
}

/* The big picture on the Playing tab; returns the source used (-1 icon). */
static int hero_picture(ShUi *ui, int g)
{
    int src = pic_source(ui, g);
    if (g == g_hero_game && src == g_hero_src) return src;
    g_hero_game = g; g_hero_src = src;
    if (src >= 0) {
        char p[160];
        data_thm_path(&ui->info[g], src, p, sizeof(p));
        if (img_read_thm(p, g_hero, PIC_W, PIC_H) == 0) return src;
        g_hero_src = src = -1;                               /* the picture file is missing: icon instead */
    }
    compose_icon_pic(ui, g, g_hero);
    return src;
}

static const u16 *card_picture(ShUi *ui, int g)
{
    int src = pic_source(ui, g);
    if (g_card[g] && g_card_src[g] == src) return g_card[g];
    if (src < 0) return NULL;                               /* drawn as an icon on dark */
    char p[160];
    static u16 tmp[PIC_W * PIC_H];
    data_thm_path(&ui->info[g], src, p, sizeof(p));
    if (img_read_thm(p, tmp, PIC_W, PIC_H) < 0) return NULL;
    if (!g_card[g]) g_card[g] = malloc(CARD_W * CARD_H * 2);
    if (!g_card[g]) return NULL;
    img_scale565(tmp, PIC_W, PIC_H, g_card[g], CARD_W, CARD_H);
    g_card_src[g] = (s8)src;
    return g_card[g];
}

/* ---- the lists ------------------------------------------------------------------ */
const char *ui_title(ShUi *ui, int g)
{
    const ShGame *x = &ui->lib->g[g];
    return ui->cfg->titles_from_file ? x->file_title : x->title;
}

static int shown_type(ShUi *ui, int g)
{
    int t = ui->lib->g[g].type;
    if (t == GT_SKIP) return 0;
    if (t == GT_PSN && !ui->cfg->show_psn) return 0;
    if (t == GT_HOMEBREW && !ui->cfg->show_homebrew) return 0;
    if (t == GT_PS1 && !ui->cfg->show_ps1) return 0;
    return 1;
}

/* The folder a game sits in below ISO/ or PSP/GAME/, "" if none. "CAT_" is dropped, as ARK does. */
static void folder_of(const char *path, char *out, int size)
{
    out[0] = 0;
    const char *rest = NULL;
    int need = 1;
    if (!strncasecmp(path, "ms0:/ISO/", 9)) rest = path + 9;
    else if (!strncasecmp(path, "ms0:/PSP/GAME/", 14)) { rest = path + 14; need = 2; }
    if (!rest) return;
    int slashes = 0;
    for (const char *c = rest; *c; c++) slashes += *c == '/';
    if (slashes < need) return;
    const char *s = strchr(rest, '/');
    int n = (int)(s - rest);
    if (n > 4 && !strncasecmp(rest, "CAT_", 4)) { rest += 4; n -= 4; }
    if (n >= size) n = size - 1;
    memcpy(out, rest, n); out[n] = 0;
}

static int in_category(ShUi *ui, int g, int c)
{
    int t = ui->lib->g[g].type;
    char f[32];
    switch (ui->cat_kind[c]) {
    case 0: return 1;
    case 1: return t == GT_ISO || t == GT_PSN;
    case 2: return t == GT_HOMEBREW;
    case 3: return t == GT_PS1;
    case 5: return ui->info[g].fav;
    case 6: folder_of(ui->lib->g[g].path, f, sizeof(f)); return t == GT_HOMEBREW && !f[0];
    case 7: folder_of(ui->lib->g[g].path, f, sizeof(f)); return (t == GT_ISO || t == GT_PSN) && !f[0];
    default: folder_of(ui->lib->g[g].path, f, sizeof(f)); return !sh_casecmp(f, ui->cat_name[c]);
    }
}

static void add_cat(ShUi *ui, const char *name, int kind)
{
    if (ui->ncats >= MAX_CATS) return;
    /* in name order; All Games (always added first) stays on top */
    int at = ui->ncats;
    while (at > 1 && sh_casecmp(ui->cat_name[at - 1], name) > 0) at--;
    for (int c = ui->ncats; c > at; c--) { memcpy(ui->cat_name[c], ui->cat_name[c - 1], 32); ui->cat_kind[c] = ui->cat_kind[c - 1]; }
    sh_strcpy(ui->cat_name[at], 32, name);
    ui->cat_kind[at] = kind;
    ui->ncats++;
}

/* The categories of the All tab: All Games first, the rest in name order.
 *   - one for every folder your games sit in below ISO/ and PSP/GAME/ (APPS, PORTS, PSP, ...)
 *   - PSP Games and PS1 Games, but only when no folder already says the same: PSP Games is left
 *     out when there is a folder called PSP, PS1 Games when there is one called PS1, PSX or PSONE,
 *     and either one when a folder holds exactly those games
 *   - Other Homebrew, Other PSP Games: what is in no folder */
static void build_categories(ShUi *ui)
{
    char keep[32], keepf[32];
    sh_strcpy(keep, sizeof(keep), ui->ncats ? ui->cat_name[ui->cat] : "");
    sh_strcpy(keepf, sizeof(keepf), ui->ncats ? ui->cat_name[ui->fcat] : "");
    ui->ncats = 0;
    int n_type[4] = { 0, 0, 0, 0 }, fav = 0, loose_hb = 0, hb_in_folder = 0, loose_psp = 0, dup_psp = 0, dup_ps1 = 0;
    for (int g = 0; g < ui->lib->n; g++) {
        if (!shown_type(ui, g)) continue;
        int t = ui->lib->g[g].type;
        char f[32];
        folder_of(ui->lib->g[g].path, f, sizeof(f));
        n_type[t == GT_ISO || t == GT_PSN ? 1 : t == GT_HOMEBREW ? 2 : 3]++;
        fav += ui->info[g].fav;
        if (t == GT_HOMEBREW) { if (f[0]) hb_in_folder++; else loose_hb++; }
        if ((t == GT_ISO || t == GT_PSN) && !f[0]) loose_psp++;
    }
    add_cat(ui, "All Games", 0);
    for (int g = 0; g < ui->lib->n && ui->ncats < MAX_CATS - 4; g++) {
        if (!shown_type(ui, g)) continue;
        char f[32];
        folder_of(ui->lib->g[g].path, f, sizeof(f));
        if (!f[0]) continue;
        int dup = 0;
        for (int c = 1; c < ui->ncats && !dup; c++) dup = !sh_casecmp(ui->cat_name[c], f);
        if (dup) continue;
        int members = 0, psp = 0, ps1 = 0;
        for (int k = 0; k < ui->lib->n; k++) {
            if (!shown_type(ui, k)) continue;
            char f2[32];
            folder_of(ui->lib->g[k].path, f2, sizeof(f2));
            if (sh_casecmp(f, f2)) continue;
            int t = ui->lib->g[k].type;
            members++; psp += t == GT_ISO || t == GT_PSN; ps1 += t == GT_PS1;
        }
        if (!sh_casecmp(f, "PSP") || (psp == members && members == n_type[1])) dup_psp = 1;
        if (!sh_casecmp(f, "PS1") || !sh_casecmp(f, "PSX") || !sh_casecmp(f, "PSONE") || (ps1 == members && members == n_type[3])) dup_ps1 = 1;
        add_cat(ui, f, 4);
    }
    (void)fav;                                   /* favourites have a tab of their own */
    if (n_type[1] && !dup_psp) add_cat(ui, "PSP Games", 1);
    if (dup_psp && loose_psp) add_cat(ui, "Other PSP Games", 7);     /* PSP games in no folder, so every game is in some category */
    if (n_type[3] && !dup_ps1) add_cat(ui, "PS1 Games", 3);
    if (loose_hb) add_cat(ui, hb_in_folder ? "Other Homebrew" : "Homebrew", hb_in_folder ? 6 : 2);
    ui->cat = ui->fcat = 0;
    for (int c = 0; c < ui->ncats; c++) {
        if (!strcmp(ui->cat_name[c], keep)) ui->cat = c;
        if (!strcmp(ui->cat_name[c], keepf)) ui->fcat = c;
        ui->cat_count[c] = 0;
        for (int g = 0; g < ui->lib->n; g++) ui->cat_count[c] += shown_type(ui, g) && in_category(ui, g, c);
    }
}

static ShUi *g_sort_ui;
static int g_sort_key;
static int title_cmp(int a, int b) { return sh_casecmp(ui_title(g_sort_ui, a), ui_title(g_sort_ui, b)); }
static int cmp_games(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb;
    const ShInfo *x = &g_sort_ui->info[a], *y = &g_sort_ui->info[b];
    long long d = 0;
    switch (g_sort_key) {
    case SORT_LAST:     d = (long long)y->last_played - x->last_played; break;
    case SORT_TIME:     d = (long long)y->cp_secs - x->cp_secs; break;
    case SORT_EMBLEM: {
        long long fx = x->em_total > 0 ? x->em_got * 1000LL / x->em_total : -1, fy = y->em_total > 0 ? y->em_got * 1000LL / y->em_total : -1;
        d = fy - fx; break;
    }
    case SORT_ADDED:    d = (long long)g_sort_ui->lib->g[b].added - g_sort_ui->lib->g[a].added; break;
    case SORT_FINISHED: d = (long long)y->finished - x->finished; break;
    default: break;
    }
    if (d) return d > 0 ? 1 : -1;
    return title_cmp(a, b);
}

static int is_grid(int t) { return t == 1 || t == TAB_FAV; }
/* Home, Favourites and Finished can each be switched off in the settings; All always shows. */
static int tab_on(const ShUi *ui, int t)
{
    return t == 0 ? ui->cfg->tab_home : t == 2 ? ui->cfg->tab_finished : t == TAB_FAV ? ui->cfg->tab_fav : 1;
}
static int *tab_cat(ShUi *ui, int t) { return t == TAB_FAV ? &ui->fcat : &ui->cat; }

/* The category a setting names. "PSP Games" and "PS1 Games" are not listed when a folder
 * says the same, so those two fall back to that folder; anything else unknown is All Games. */
static int cat_named(const ShUi *ui, const char *name)
{
    for (int c = 0; c < ui->ncats; c++) if (!sh_casecmp(ui->cat_name[c], name)) return c;
    const char *alt[3] = { NULL, NULL, NULL };
    if (!sh_casecmp(name, "PSP Games")) alt[0] = "PSP";
    if (!sh_casecmp(name, "PS1 Games")) { alt[0] = "PSX"; alt[1] = "PS1"; alt[2] = "PSONE"; }
    for (int k = 0; k < 3 && alt[k]; k++)
        for (int c = 0; c < ui->ncats; c++) if (!sh_casecmp(ui->cat_name[c], alt[k])) return c;
    return 0;
}

static void build_lists(ShUi *ui)
{
    int keep[4];
    for (int t = 0; t < 4; t++) keep[t] = ui->count[t] ? ui->list[t][ui->sel[t]] : -1;
    for (int t = 0; t < 4; t++) ui->count[t] = 0;
    for (int g = 0; g < ui->lib->n; g++) {
        if (!shown_type(ui, g)) continue;
        ShInfo *in = &ui->info[g];
        if (in->playing && (!in->hidden || ui->cfg->show_hidden)) ui->list[0][ui->count[0]++] = g;
        if (in_category(ui, g, ui->cat)) ui->list[1][ui->count[1]++] = g;
        if (in->finished) ui->list[2][ui->count[2]++] = g;
        if (in->fav && in_category(ui, g, ui->fcat)) ui->list[TAB_FAV][ui->count[TAB_FAV]++] = g;
    }
    g_sort_ui = ui;
    for (int t = 0; t < 4; t++) {
        g_sort_key = ui->cfg->sort[t];
        qsort(ui->list[t], ui->count[t], sizeof(int), cmp_games);
        ui->sel[t] = 0;
        for (int i = 0; i < ui->count[t]; i++) if (ui->list[t][i] == keep[t]) ui->sel[t] = i;
        if (ui->sel[t] >= ui->count[t]) ui->sel[t] = ui->count[t] ? ui->count[t] - 1 : 0;
    }
}

static void build_items(ShUi *ui);
static void restore_place(ShUi *ui);
static void move_sel(ShUi *ui, int d);

void ui_rebuild(ShUi *ui)
{
    forget_pictures();
    free(g_pack);
    g_pack = scan_small_load(ui->lib);
    build_categories(ui);
    build_lists(ui);
    if (ui->overlay == OV_SETTINGS) build_items(ui);    /* after a rescan: the library line */
    ui->dirty = 1;
}

void ui_init(ShUi *ui)
{
    ui->tab = ui->cfg->open_tab < 3 ? ui->cfg->open_tab : ui->cfg->open_tab == 4 ? TAB_FAV : 0;
    for (int i = 0; i < SH_MAX_GAMES; i++) g_card_src[i] = -2;
    ui_rebuild(ui);
    /* every start: All and Favourites open on the categories set in the settings */
    ui->cat = cat_named(ui, ui->cfg->all_default);
    ui->fcat = cat_named(ui, ui->cfg->fav_default);
    build_lists(ui);
    if (ui->cfg->open_tab == 3) restore_place(ui);
    if (ui->tab == 0 && !ui->count[0]) ui->tab = 1;
    if (!tab_on(ui, ui->tab)) ui->tab = 1;
}

void ui_remember(ShUi *ui)
{
    ShConfig *c = ui->cfg;
    int g = ui_selected_game(ui);
    c->last_tab = ui->tab;
    sh_strcpy(c->last_cat, sizeof(c->last_cat), ui->ncats && is_grid(ui->tab) ? ui->cat_name[*tab_cat(ui, ui->tab)] : "");
    sh_strcpy(c->last_game, sizeof(c->last_game), g >= 0 ? ui->lib->g[g].path : "");
}

static void restore_place(ShUi *ui)
{
    const ShConfig *c = ui->cfg;
    ui->tab = c->last_tab;
    for (int k = 0; k < ui->ncats && is_grid(ui->tab); k++)        /* the category you were in, on the tab you were on */
        if (!strcmp(ui->cat_name[k], c->last_cat)) { *tab_cat(ui, ui->tab) = k; build_lists(ui); break; }
    int t = ui->tab;
    for (int i = 0; i < ui->count[t]; i++)
        if (!strcmp(ui->lib->g[ui->list[t][i]].path, c->last_game)) { ui->sel[t] = i; break; }
}

int ui_selected_game(ShUi *ui)
{
    int t = ui->tab;
    return ui->count[t] ? ui->list[t][ui->sel[t]] : -1;
}

void ui_message(ShUi *ui, const char *a, const char *b, const char *c)
{
    sh_strcpy(ui->msg[0], 64, a ? a : "");
    sh_strcpy(ui->msg[1], 64, b ? b : "");
    sh_strcpy(ui->msg[2], 64, c ? c : "");
    ui->overlay = OV_MESSAGE;
    ui->confirm_action = 0;
    ui->dirty = 1;
}

/* ---- drawing helpers ------------------------------------------------------------- */
static void wrap2(const char *t, int cols, char out[2][64])
{
    out[0][0] = out[1][0] = 0;
    if (u8_cols(t) <= cols) { u8_cut(t, cols, out[0], 64); return; }
    /* break at the last space within the first line, else after `cols` characters */
    int brk = u8_bytes(t, cols), sp = -1;
    for (int i = 0; i <= brk && t[i]; i++) if (t[i] == ' ') sp = i;
    if (sp > 0) brk = sp;
    int n = brk < 63 ? brk : 63;
    memcpy(out[0], t, n); out[0][n] = 0;
    const char *rest = t + brk;
    while (*rest == ' ') rest++;
    u8_cut(rest, cols, out[1], 64);
}

/* out must be an array: its size bounds the copy (UTF-8 can take 3 bytes a character) */
#define cut(t, cols, out) u8_cut((t), (cols), (out), (int)sizeof(out))

static const char *const g_sort_label[SORT_COUNT] = { "Last played", "Name A-Z", "Time played", "Emblem progress", "Recently added", "Date completed" };

static void titlebar(ShUi *ui, const ShSurf *s)
{
    gfx_rect(s, 0, 0, SCR_W, 15, C_BAR);
    gfx_hline(s, 0, 15, SCR_W, C_LINE);
    gfx_text(s, 6, 4, "SHELF", C_AMBER);
    int x = 6 + 40 + 12;
    /* Names only: how many games a tab holds is on the line below it. SHELF, the tabs and
     * the clock are spaced evenly: the room left over is shared out in equal gaps, the one
     * after SHELF and the one before the clock included. It is worked out from the clock as
     * it reads now, so the gaps stay equal when the battery shows 100% or a "+". */
    static const int order[4] = { 0, 1, TAB_FAV, 2 };          /* as L and R step through them */
    const char *tabs[4] = { "Home", SH_TAB_ALL, "Completed", "Favourites" };
    int n = 0, wsum = 0;
    for (int k = 0; k < 4; k++) if (tab_on(ui, order[k])) { n++; wsum += gfx_tw(tabs[order[k]]); }
    int left = 6 + gfx_tw("SHELF"), right = SCR_W - 6 - gfx_tw(ui->clock);
    int room = right - left - wsum;
    if (room < 4 * (n + 1)) room = 4 * (n + 1);
    int used = 0, i = 0;
    for (int k = 0; k < 4; k++) {
        int t = order[k];
        if (!tab_on(ui, t)) continue;
        i++;
        x = left + room * i / (n + 1) + used;                  /* i gaps and the tabs before this one */
        gfx_text(s, x, 4, tabs[t], t == ui->tab ? C_AMBER : C_GREY);
        if (t == ui->tab) gfx_hline(s, x, 13, gfx_tw(tabs[t]), C_AMBER);
        used += gfx_tw(tabs[t]);
    }
    gfx_text_r(s, SCR_W - 6, 4, ui->clock, C_GREY);
}

typedef struct { const char *k; const char *label; } Key;
static void keybar(const ShSurf *s, const Key *keys, int n)
{
    int y = 260;
    gfx_rect(s, 0, y - 4, SCR_W, SCR_H - y + 4, C_KEYBAR);
    gfx_hline(s, 0, y - 4, SCR_W, C_LINE);
    int total = 0;                               /* spread out up to 10 pixels apart, closer when there are many */
    for (int i = 0; i < n; i++) {
        const char *k = keys[i].k;
        total += (strlen(k) == 1 && strchr("XOSTUDLR", k[0]) ? 10 : gfx_tw(k) + 3) + gfx_tw(keys[i].label);
    }
    int gap = n > 1 ? (SCR_W - 10 - total) / (n - 1) : 10;
    if (gap > 10) gap = 10;
    if (gap < 3) gap = 3;
    /* START and SELECT, when a screen has them, sit together at the right end */
    int tail = n, tw = 0;
    for (int i = 0; i < n; i++) if (!strcmp(keys[i].k, "START") || !strcmp(keys[i].k, "SELECT")) { tail = i; break; }
    for (int i = tail; i < n; i++) tw += gfx_tw(keys[i].k) + 3 + gfx_tw(keys[i].label) + (i < n - 1 ? gap : 0);
    int x = 6;
    for (int i = 0; i < n; i++) {
        const char *k = keys[i].k;
        if (i == tail && SCR_W - 6 - tw > x) x = SCR_W - 6 - tw;
        if (strlen(k) == 1 && strchr("XOSTUDLR", k[0])) { gfx_button(s, x, y, k[0]); x += 10; }
        else { gfx_text(s, x, y, k, C_WHITE); x += gfx_tw(k) + 3; }
        x += gfx_text(s, x, y, keys[i].label, C_GREY) + gap;
    }
}

static const Key g_home_keys[] = { { "X", "Play" }, { "S", "Info" }, { "T", "Sort" }, { "SELECT", "Options" }, { "START", "Settings" } };
static const Key g_back_keys[] = { { "X", "Play" }, { "S", "Info" }, { "T", "Sort" }, { "O", "Back" }, { "SELECT", "Options" }, { "START", "Settings" } };

/* a stand-in tile for a game without an icon */
static void no_icon(ShUi *ui, const ShSurf *s, int x, int y, int w, int h, int g)
{
    u32 k = ui->lib->g[g].key;
    gfx_rect(s, x, y, w, h, RGB(40 + (k & 63), 40 + ((k >> 8) & 63), 50 + ((k >> 16) & 63)));
    char l[2][64];
    wrap2(ui_title(ui, g), w / 8, l);
    int lines = l[1][0] ? 2 : 1, ty = y + h / 2 - lines * 5 + 1;
    for (int i = 0; i < lines; i++) {
        char c[64]; cut(l[i], w / 8, c);
        gfx_text_c(s, x + w / 2 + 1, ty + 1 + i * 10, c, C_BLACK);
        gfx_text_c(s, x + w / 2, ty + i * 10, c, C_WHITE);
    }
}

static void draw_small(ShUi *ui, const ShSurf *s, int x, int y, int g)
{
    const u16 *ic = small_icon(ui, g);
    if (ic) gfx_blit565(s, x, y, ic, SMALL_W, SMALL_H);
    else no_icon(ui, s, x, y, SMALL_W, SMALL_H, g);
}

static char badge_of(ShUi *ui, int g)
{
    ShInfo *in = &ui->info[g];
    if (in->finished) return 'C';
    if (ui->lib->g[g].type != GT_PS1) {         /* what a start from Home goes back to: the suspend, or a state */
        if (in->sq_newest == 0) return 'P';
        if (in->sq_newest > 0) return 'S';
    }
    if (in->playing) return 'D';
    return 0;
}

/* The Sidequest slot a start of this game goes back to, or -1 for a plain start.
 * From Home a game goes straight to its newest slot; "Start from..." can name another, or none. */
static int resume_slot(ShUi *ui, int g, int from_home)
{
    ShInfo *in = &ui->info[g];
    if (ui->lib->g[g].type == GT_PS1) return -1;
    if (ui->act_from == 1 && ui->act_game == g) return in->sq_has[ui->act_slot] ? ui->act_slot : -1;
    if (ui->act_from == 2 && ui->act_game == g) return -1;
    return from_home ? in->sq_newest : -1;
}

/* ---- Playing ---------------------------------------------------------------------- */
static void draw_playing(ShUi *ui, const ShSurf *s)
{
    char b[64], l[2][64];
    if (!ui->count[0]) {
        gfx_rect(s, 6, 20, 468, 230, C_CARD);
        gfx_text_c(s, SCR_W / 2, 110, "Nothing on Home yet", C_WHITE);
        gfx_text_c(s, SCR_W / 2, 126, "Start a game from " SH_TAB_ALL " with X, or press R.", C_GREY);
        keybar(s, g_home_keys, 5);
        return;
    }
    int g = ui->list[0][ui->sel[0]];
    ShInfo *in = &ui->info[g];
    gfx_rect(s, 6, 20, 468, 144, C_CARD);
    gfx_frame(s, 6, 20, 468, 144, C_LINE, 1);
    int src = hero_picture(ui, g);
    gfx_blit565(s, 10, 24, g_hero, PIC_W, PIC_H);
    gfx_frame(s, 9, 23, 242, 138, C_FRAME, 1);
    /* The label says what X will go back to, whichever picture is shown: the suspend, or a
     * state. A game with no slot has no label. */
    int rs = resume_slot(ui, g, 1);
    char tagb[16];
    const char *tag = rs == 0 ? "SUSPENDED" : NULL;
    if (rs > 0) { sh_snprintf(tagb, sizeof(tagb), "STATE %d", rs); tag = tagb; }
    (void)src;
    if (tag) { gfx_rect(s, 14, 146, gfx_tw(tag) + 6, 11, C_BLACK); gfx_text(s, 17, 148, tag, rs == 0 ? C_AMBER : C_GREY); }

    int x = 260, y = 28;
    wrap2(ui_title(ui, g), 26, l);
    gfx_text(s, x, y, l[0], C_WHITE); y += 10;
    if (l[1][0]) { gfx_text(s, x, y, l[1], C_WHITE); y += 10; }
    y += 4;
    gfx_text(s, x, y, "Last journey entry", C_GREY); y += 10;
    if (in->cp_last[0]) {
        wrap2(in->cp_last, 26, l);
        gfx_text(s, x, y, l[0], C_RED); y += 10;
        if (l[1][0]) { gfx_text(s, x, y, l[1], C_RED); y += 10; }
    } else { gfx_text(s, x, y, "none yet", C_DIM); y += 10; }
    y += 6;
    gfx_text(s, x, y, "Played", C_GREY);
    if (in->cp_secs) sh_format_played(in->cp_secs, b, sizeof(b)); else sh_strcpy(b, sizeof(b), "-");
    gfx_text(s, x + 72, y, b, C_WHITE); y += 12;
    gfx_text(s, x, y, "Last", C_GREY);
    sh_format_ago(ui->now, in->last_played, b, sizeof(b));
    gfx_text(s, x + 72, y, b, C_WHITE); y += 12;
    gfx_text(s, x, y, "Emblem", C_GREY);
    if (in->em_total >= 0) {
        sh_snprintf(b, sizeof(b), "%d / %d", in->em_got, in->em_total);
        gfx_text(s, x + 72, y, b, C_GOLD); y += 11;
        gfx_bar(s, x, y, 204, 4, in->em_got, in->em_total, C_GOLD); y += 10;
    } else { gfx_text(s, x + 72, y, "no game file", C_DIM); y += 14; }
    if (rs >= 0) {
        char nm[16];
        y += 2;
        if (rs == 0) sh_strcpy(nm, sizeof(nm), "Suspend"); else sh_snprintf(nm, sizeof(nm), "State %d", rs);
        gfx_text(s, x, y, nm, C_GREY);
        sh_format_date(in->sq_time[rs], b, sizeof(b), 1);
        gfx_text(s, x + 72, y, b + 0, C_AMBER);
    }
    gfx_button(s, x, 148, 'X');
    if (rs == 0) gfx_text(s, x + 12, 148, "Resume suspended game", C_AMBER);
    else if (rs > 0) { sh_snprintf(b, sizeof(b), "Continue from state %d", rs); gfx_text(s, x + 12, 148, b, C_AMBER); }
    else gfx_text(s, x + 12, 148, "Start game", C_WHITE);

    sh_snprintf(b, sizeof(b), "(%d)", ui->count[0]);
    gfx_text(s, 8 + gfx_text(s, 8, 170, "PLAYING", C_WHITE) + 8, 170, b, C_GREY);
    const char *sn = g_sort_label[ui->cfg->sort[0]];
    gfx_button(s, SCR_W - 8 - gfx_tw(sn) - 11, 170, 'T');
    gfx_text_r(s, SCR_W - 8, 170, sn, C_GREY);

    int first = ui->top[0];
    if (ui->sel[0] < first) first = ui->sel[0];
    if (ui->sel[0] >= first + 5) first = ui->sel[0] - 4;
    ui->top[0] = first;
    for (int i = 0; i < 5 && first + i < ui->count[0]; i++) {
        int gg = ui->list[0][first + i], x0 = 8 + i * (CARD_W + 7), cy = 183;
        const u16 *card = card_picture(ui, gg);
        if (card) gfx_blit565(s, x0, cy, card, CARD_W, CARD_H);
        else { gfx_rect(s, x0, cy, CARD_W, CARD_H, RGB(10, 12, 16)); draw_small(ui, s, x0 + 7, cy + 4, gg); }
        int on = first + i == ui->sel[0];
        gfx_frame(s, x0 - 2, cy - 2, CARD_W + 4, CARD_H + 4, on ? C_AMBER : C_FRAME, on ? 2 : 1);
        { int rs = resume_slot(ui, gg, 1); if (rs >= 0) gfx_badge(s, x0 + CARD_W - 11, cy + 2, rs == 0 ? 'P' : 'S'); }
        char c[16];
        cut(ui_title(ui, gg), 10, c);
        gfx_text(s, x0, cy + CARD_H + 4, c, on ? C_WHITE : C_GREY);
        if (ui->info[gg].cp_secs) sh_format_played(ui->info[gg].cp_secs, b, sizeof(b)); else sh_strcpy(b, sizeof(b), "");
        gfx_text(s, x0, cy + CARD_H + 13, b, C_DIM);
    }
    if (first > 0) gfx_button(s, 0, 203, 'L');
    if (first + 5 < ui->count[0]) gfx_button(s, SCR_W - 8, 203, 'R');
    keybar(s, g_home_keys, 5);
}

/* ---- All ------------------------------------------------------------------------- */
/* Games per row (Settings > Shelf): icon size, rows shown, spacing, names under icons. */
typedef struct { int cols, w, h, rows, gx, gy, names; } GridLayout;
static const GridLayout g_layouts[4] = {
    { 3, 128, 72, 2, 16, 11, 1 },
    { 4,  92, 52, 3, 14,  7, 0 },
    { 5,  86, 48, 3,  8, 10, 0 },
    { 6, SMALL_W, SMALL_H, 4, 7, 4, 0 },
};
static const GridLayout *grid_layout(const ShUi *ui)
{
    int c = ui->cfg->grid_cols;
    return &g_layouts[(c >= 3 && c <= 6 ? c : 6) - 3];
}

/* An icon at the grid size, scaled once from the cached 144x80 icon. */
static const u16 *grid_icon(ShUi *ui, int g, const GridLayout *L)
{
    if (L->cols == 6) return small_icon(ui, g);
    if (g < 0 || g >= SH_MAX_GAMES) return NULL;
    if (g_grid_w != L->w) {                               /* the size changed: start again */
        for (int i = 0; i < SH_MAX_GAMES; i++) { free(g_grid[i]); g_grid[i] = NULL; g_grid_state[i] = 0; }
        g_grid_n = 0; g_grid_w = L->w;
    }
    if (!g_grid_state[g]) {
        g_grid_state[g] = 2;
        const u16 *src = ui->lib->g[g].has_icon ? big_icon(ui, g) : NULL;
        u16 *b = src ? malloc(L->w * L->h * 2) : NULL;
        if (b) { img_scale565(src, ICON_W, ICON_H, b, L->w, L->h); g_grid[g] = b; g_grid_state[g] = 1; g_grid_n++; }
    }
    return g_grid[g];
}

/* Keeps memory bounded: when many are held, drop those not near the screen. */
static void grid_trim(ShUi *ui, int first, int last)
{
    int t = is_grid(ui->tab) ? ui->tab : 1;
    if (g_grid_n <= 64) return;
    static u8 keep[SH_MAX_GAMES];
    memset(keep, 0, sizeof(keep));
    for (int i = first; i <= last && i < ui->count[t]; i++) if (i >= 0) keep[ui->list[t][i]] = 1;
    for (int g = 0; g < SH_MAX_GAMES; g++)
        if (g_grid[g] && !keep[g]) { free(g_grid[g]); g_grid[g] = NULL; g_grid_state[g] = 0; g_grid_n--; }
}

/* Called when no button is down: makes one icon of the rows just off screen
 * ready, so moving on does not wait for the Memory Stick. */
int ui_idle(ShUi *ui)
{
    int t = ui->tab;
    if (!is_grid(t) || ui->overlay != OV_NONE) return 0;
    const GridLayout *L = grid_layout(ui);
    if (L->cols == 6 || g_grid_w != L->w) return 0;       /* the small icons are all in memory already */
    int page = L->rows * L->cols, first = ui->top[t] * L->cols;
    for (int i = first; i < first + 2 * page; i++) {      /* this page and the next, then the one before */
        if (i >= ui->count[t]) break;
        int g = ui->list[t][i];
        if (!g_grid_state[g]) { grid_icon(ui, g, L); return 1; }
    }
    for (int i = first - 1; i >= 0 && i >= first - page; i--) {
        int g = ui->list[t][i];
        if (!g_grid_state[g]) { grid_icon(ui, g, L); return 1; }
    }
    return 0;
}

static void draw_all(ShUi *ui, const ShSurf *s)
{
    char b[96];
    int t = ui->tab == TAB_FAV ? TAB_FAV : 1;
    const GridLayout *L = grid_layout(ui);
    /* which category this is; Triangle opens the list of them */
    gfx_button(s, 8, 20, 'T');
    int x = 20 + gfx_text(s, 20, 20, ui->cat_name[*tab_cat(ui, t)], C_AMBER);
    sh_snprintf(b, sizeof(b), "  %d Game%s", ui->count[t], ui->count[t] == 1 ? "" : "s");
    gfx_text(s, x, 20, b, C_GREY);
    sh_snprintf(b, sizeof(b), "%s  %d of %d", g_sort_label[ui->cfg->sort[t]], (ui->count[t] ? ui->sel[t] + 1 : 0), ui->count[t]);
    gfx_text_r(s, SCR_W - 8, 20, b, C_DIM);
    int n = ui->count[t];
    int sel = ui->sel[t], row = sel / L->cols;
    int top = ui->top[t];
    if (row < top) top = row;
    if (row >= top + L->rows) top = row - L->rows + 1;
    ui->top[t] = top;
    int span = L->cols * L->w + (L->cols - 1) * L->gx;
    int x0 = (SCR_W - span) / 2, y0 = L->cols == 6 ? 33 : 34;
    int pitch = L->h + L->gy + (L->names ? 8 : 0);
    grid_trim(ui, (top - L->rows) * L->cols, (top + 2 * L->rows) * L->cols);
    for (int r = 0; r < L->rows; r++)
        for (int c = 0; c < L->cols; c++) {
            int i = (top + r) * L->cols + c;
            if (i >= n) break;
            int g = ui->list[t][i];
            int gx = x0 + c * (L->w + L->gx), gy = y0 + r * pitch;
            const u16 *ic = grid_icon(ui, g, L);
            if (ic) gfx_blit565(s, gx, gy, ic, L->w, L->h);
            else no_icon(ui, s, gx, gy, L->w, L->h, g);
            int on = i == sel;
            gfx_frame(s, gx - 2, gy - 2, L->w + 4, L->h + 4, on ? C_AMBER : RGB(44, 52, 64), on ? 2 : 1);
            char bd = badge_of(ui, g);
            if (bd) gfx_badge(s, gx + L->w - 10, gy + 1, bd);
            if (ui->info[g].fav) gfx_badge(s, gx + 1, gy + 1, 'F');
            if (L->names) {
                char t[64];
                cut(ui_title(ui, g), L->w / 8, t);
                gfx_text(s, gx, gy + L->h + 4, t, on ? C_WHITE : C_GREY);
            }
        }
    if (!n && t == TAB_FAV && !*tab_cat(ui, t)) {
        gfx_text_c(s, SCR_W / 2, 100, "No favourites yet", C_WHITE);
        gfx_text_c(s, SCR_W / 2, 118, "On a game, press SELECT and pick 'Add to Favourites'.", C_GREY);
    } else if (!n) gfx_text_c(s, SCR_W / 2, 110, "No games here", C_GREY);
    int nrows = (n + L->cols - 1) / L->cols;
    if (nrows > L->rows) {                               /* where you are in the list */
        int th = 174, bh = th * L->rows / nrows, by = (th - bh) * top / (nrows - L->rows);
        if (bh < 8) bh = 8;
        gfx_rect(s, SCR_W - 4, 33, 2, th, C_TRACK);
        gfx_rect(s, SCR_W - 4, 33 + (by + bh > th ? th - bh : by), 2, bh, C_AMBER);
    }
    /* the selected game */
    int y = 212;
    gfx_rect(s, 6, y, 468, 40, C_CARD);
    gfx_frame(s, 6, y, 468, 40, C_LINE, 1);
    if (n) {
        int g = ui->list[t][sel];
        ShInfo *in = &ui->info[g];
        char t[64];
        cut(ui_title(ui, g), 56, t);
        gfx_text(s, 14, y + 5, t, C_WHITE);
        static const char *const kinds[] = { "PSP", "PSN", "Homebrew", "PS1" };
        int xx = 14;
        if (in->fav) { gfx_badge(s, xx, y + 16, 'F'); xx += 14; }
        xx += gfx_text(s, xx, y + 17, kinds[ui->lib->g[g].type & 3], C_DIM) + 16;
        if (in->finished) { sh_format_date(in->finished, t, sizeof(t), 0); sh_snprintf(b, sizeof(b), "Completed %s", t); xx += gfx_text(s, xx, y + 17, b, C_GREEN) + 16; }
        else if (in->cp_secs) { sh_format_played(in->cp_secs, t, sizeof(t)); sh_snprintf(b, sizeof(b), "Played %s", t); xx += gfx_text(s, xx, y + 17, b, C_WHITE) + 16; }
        else if (!in->playing) xx += gfx_text(s, xx, y + 17, "Not started", C_GREY) + 16;
        if (in->em_total >= 0) { sh_snprintf(b, sizeof(b), "Emblem %d / %d", in->em_got, in->em_total); gfx_text(s, xx, y + 17, b, C_GOLD); }
        int lx = 14;
        static const struct { char k; const char *t; } leg[4] = { { 'D', "playing" }, { 'P', "suspended" }, { 'S', "state" }, { 'C', "completed" } };
        for (int k = 0; k < 4; k++) { gfx_badge(s, lx, y + 28, leg[k].k); lx += 12 + gfx_text(s, lx + 12, y + 29, leg[k].t, C_DIM) + 10; }
    }
    static const Key k[] = { { "X", "Play" }, { "S", "Info" }, { "T", "Category" }, { "O", "Back" }, { "SELECT", "Options" }, { "START", "Settings" } };
    if (t == 1 && !tab_on(ui, 0)) {              /* no Home to go back to: O does nothing here */
        static const Key k2[] = { { "X", "Play" }, { "S", "Info" }, { "T", "Category" }, { "SELECT", "Options" }, { "START", "Settings" } };
        keybar(s, k2, 5);
    } else keybar(s, k, 6);
}

/* ---- Finished -------------------------------------------------------------------- */
static void draw_finished(ShUi *ui, const ShSurf *s)
{
    char b[64], t[64];
    sh_snprintf(b, sizeof(b), "(%d)", ui->count[2]);
    gfx_text(s, 8 + gfx_text(s, 8, 22, "COMPLETED", C_WHITE) + 8, 22, b, C_GREY);
    const char *sn = g_sort_label[ui->cfg->sort[2]];
    gfx_button(s, SCR_W - 8 - gfx_tw(sn) - 11, 22, 'T');
    gfx_text_r(s, SCR_W - 8, 22, sn, C_GREY);
    int top = ui->top[2];
    if (ui->sel[2] < top) top = ui->sel[2];
    if (ui->sel[2] >= top + 3) top = ui->sel[2] - 2;
    ui->top[2] = top;
    int y = 36;
    for (int i = top; i < ui->count[2] && i < top + 3; i++) {
        int g = ui->list[2][i];
        ShInfo *in = &ui->info[g];
        int on = i == ui->sel[2];
        gfx_rect(s, 6, y, 468, 52, on ? C_CARD2 : C_CARD);
        gfx_frame(s, 6, y, 468, 52, on ? C_AMBER : C_LINE, 1);
        draw_small(ui, s, 12, y + 6, g);
        gfx_badge(s, 12 + SMALL_W - 10, y + 7, 'C');
        cut(ui_title(ui, g), 30, t);
        gfx_text(s, 94, y + 6, t, C_WHITE);
        sh_format_date(in->finished, t, sizeof(t), 0);
        sh_snprintf(b, sizeof(b), "Completed %s", t);
        gfx_text_r(s, 466, y + 6, b, C_GREEN);
        gfx_text(s, 94, y + 18, "Played", C_GREY);
        if (in->cp_secs) sh_format_played(in->cp_secs, b, sizeof(b)); else sh_strcpy(b, sizeof(b), "-");
        gfx_text(s, 150, y + 18, b, C_WHITE);
        gfx_text(s, 230, y + 18, "Emblem", C_GREY);
        if (in->em_total >= 0) {
            sh_snprintf(b, sizeof(b), "%d / %d", in->em_got, in->em_total);
            gfx_text(s, 286, y + 18, b, C_GOLD);
            gfx_bar(s, 286 + gfx_tw(b) + 8, y + 20, 80, 4, in->em_got, in->em_total, C_GOLD);
        } else gfx_text(s, 286, y + 18, "no game file", C_DIM);
        gfx_text(s, 94, y + 31, "Last entry", C_GREY);
        cut(in->cp_last[0] ? in->cp_last : "-", 34, t);
        gfx_text(s, 182, y + 31, t, in->cp_last[0] ? C_RED : C_DIM);
        y += 58;
    }
    if (!ui->count[2]) {
        gfx_text_c(s, SCR_W / 2, 110, "No completed games yet", C_WHITE);
        gfx_text_c(s, SCR_W / 2, 126, "On a game, press SELECT and pick 'Mark as completed'.", C_GREY);
    } else if (ui->count[2] <= 3) gfx_text(s, 8, y + 6, "Games land here when you pick 'Mark as completed' (SELECT).", C_DIM);
    keybar(s, g_back_keys, 6);
}

/* ---- Info (Square) ------------------------------------------------------------------ */
#define J_PER_PAGE 14
/* Two pages, L and R switch: Emblem first, then the journey. ov_sel says which (0, 1). */
static void open_info(ShUi *ui)
{
    int g = ui_selected_game(ui);
    if (g < 0) return;
    if (!ui->journey) ui->journey = malloc(sizeof(ShEntry) * 70);
    ui->njourney = ui->journey ? data_journey(ui->lib->g[g].id, ui->journey, 70, &ui->journey_total) : 0;
    ui->ov_page = 0;
    ui->ov_sel = ui->info[g].em_total < 0 && ui->njourney ? 1 : 0;     /* nothing for Emblem to show: start on the journey */
    ui->overlay = OV_INFO;
}

static void draw_info(ShUi *ui, const ShSurf *s)
{
    int g = ui_selected_game(ui);
    if (g < 0) return;
    ShInfo *in = &ui->info[g];
    char b[64], t[64];
    int journey = ui->ov_sel == 1, pages = 1;
    gfx_dim(s, 0, 0, SCR_W, SCR_H, 35);
    gfx_rect(s, 12, 20, 456, 232, C_PANEL);
    gfx_frame(s, 12, 20, 456, 232, C_AMBER, 1);
    cut(ui_title(ui, g), 38, t);
    gfx_text(s, 22, 28, t, C_WHITE);
    if (in->cp_secs) { sh_format_played(in->cp_secs, b, sizeof(b)); strcat(b, " played"); }
    else sh_strcpy(b, sizeof(b), in->playing ? "" : "not started");
    gfx_text_r(s, 458, 28, b, C_GREY);
    gfx_hline(s, 20, 40, 440, C_LINE);
    /* the two pages, the open one underlined */
    gfx_text(s, 22, 48, "EMBLEM", journey ? C_DIM : C_GOLD);
    gfx_text(s, 22 + 48 + 16, 48, "JOURNEY", journey ? C_RED : C_DIM);
    if (journey) gfx_hline(s, 22 + 48 + 16, 57, 56, C_RED); else gfx_hline(s, 22, 57, 48, C_GOLD);

    if (journey) {
        sh_snprintf(b, sizeof(b), "%d entr%s", ui->journey_total, ui->journey_total == 1 ? "y" : "ies");
        gfx_text_r(s, 458, 48, b, C_DIM);
        if (!ui->njourney) {
            gfx_text(s, 30, 76, "No entries yet.", C_GREY);
            gfx_text(s, 30, 94, "Checkpoint writes a line here each time a save", C_DIM);
            gfx_text(s, 30, 105, "shows a new place.", C_DIM);
        } else {
            pages = (ui->njourney + J_PER_PAGE - 1) / J_PER_PAGE;
            if (ui->ov_page >= pages) ui->ov_page = pages - 1;
            int y = 66;
            for (int i = ui->ov_page * J_PER_PAGE; i < ui->njourney && i < (ui->ov_page + 1) * J_PER_PAGE; i++) {
                ShEntry *e = &ui->journey[i];
                sh_format_date(e->when, t, sizeof(t), 1);
                gfx_text(s, 22, y, t, C_RED);
                cut(e->text, 29, t);
                gfx_text(s, 160, y, t, C_WHITE);
                sh_format_played(e->secs, b, sizeof(b));
                gfx_text_r(s, 458, y, b, C_DIM);
                y += 12;
            }
            if (pages > 1) { sh_snprintf(b, sizeof(b), "page %d/%d", ui->ov_page + 1, pages); gfx_text_r(s, 458, 238, b, C_DIM); }
        }
    } else if (in->em_total < 0) {
        gfx_text(s, 30, 76, "No Emblem file for this game.", C_GREY);
        gfx_text(s, 30, 94, "Games with a file in ms0:/EMBLEM/games/ show their", C_DIM);
        gfx_text(s, 30, 105, "list here.", C_DIM);
        const u16 *ic = big_icon(ui, g);
        if (ic) { gfx_blit565(s, 240 - 72, 140, ic, ICON_W, ICON_H); gfx_frame(s, 240 - 73, 139, 146, 82, C_LINE, 1); }
    } else {
        sh_snprintf(b, sizeof(b), "%d / %d", in->em_got, in->em_total);
        gfx_text_r(s, 458, 48, b, C_GOLD);
        gfx_bar(s, 22, 64, 436, 3, in->em_got, in->em_total, C_GOLD);
        int y = 76;
        if (in->em_nrecent) { gfx_text(s, 22, y, "Latest", C_DIM); y += 14; }
        for (int k = 0; k < in->em_nrecent; k++) {
            gfx_dot(s, 24, y, C_GOLD, 1);
            cut(in->em_recent[k], 36, t);
            gfx_text(s, 36, y, t, C_WHITE);
            sh_format_date(in->em_recent_time[k], t, sizeof(t), 0);
            gfx_text_r(s, 458, y, t, C_DIM);
            y += 14;
        }
        if (!in->em_nrecent) { gfx_text(s, 36, y, "None unlocked yet", C_DIM); y += 14; }
        y += 4;
        gfx_hline(s, 22, y, 436, C_LINE);
        y += 8;
        gfx_dot(s, 24, y, C_DIM, 0);
        sh_snprintf(b, sizeof(b), "%d still locked", in->em_total - in->em_got);
        gfx_text(s, 36, y, b, C_DIM);
    }
    Key k[4]; int n = 0;
    k[n].k = "L/R"; k[n++].label = journey ? "Emblem" : "Journey";
    if (journey && pages > 1) { k[n].k = "UP/DOWN"; k[n++].label = "Page"; }
    k[n].k = "X"; k[n++].label = "Play";
    k[n].k = "O"; k[n++].label = "Back";
    keybar(s, k, n);
}

/* ---- popups --------------------------------------------------------------------------- */
static void popup(const ShSurf *s, const char *title, const char *const *rows, const char *const *vals, int n, int cur,
                  int w, const char *note)
{
    int h = 30 + n * 16 + (note ? 14 : 0);
    int x0 = (SCR_W - w) / 2, y0 = (SCR_H - 16 - h) / 2;
    gfx_dim(s, 0, 0, SCR_W, SCR_H, 35);
    gfx_rect(s, x0, y0, w, h, C_PANEL);
    gfx_frame(s, x0, y0, w, h, C_AMBER, 1);
    char t[64];
    cut(title, (w - 20) / 8, t);
    gfx_text(s, x0 + 10, y0 + 8, t, C_AMBER);
    gfx_hline(s, x0 + 6, y0 + 20, w - 13, C_LINE);
    int y = y0 + 26;
    for (int i = 0; i < n; i++) {
        if (i == cur) gfx_rect(s, x0 + 4, y - 3, w - 8, 14, C_SEL);
        gfx_text(s, x0 + 12, y, rows[i], i == cur ? C_WHITE : C_GREY);
        if (vals && vals[i] && vals[i][0]) gfx_text_r(s, x0 + w - 12, y, vals[i], i == cur ? C_AMBER : C_GREY);
        y += 16;
    }
    if (note) gfx_text(s, x0 + 12, y + 2, note, C_DIM);
}

static void open_sort(ShUi *ui)
{
    static const int playing[] = { SORT_LAST, SORT_NAME, SORT_TIME, SORT_EMBLEM, SORT_ADDED };
    static const int finished[] = { SORT_FINISHED, SORT_NAME, SORT_TIME, SORT_EMBLEM };
    const int *o = ui->tab == 2 ? finished : playing;
    ui->nsort = ui->tab == 2 ? 4 : 5;
    ui->ov_sel = 0;
    for (int i = 0; i < ui->nsort; i++) { ui->sort_opts[i] = o[i]; if (o[i] == ui->cfg->sort[ui->tab]) ui->ov_sel = i; }
    if (ui->tab != 2 && ui->count[ui->tab] > 1) ui->sort_opts[ui->nsort++] = SORT_COUNT;   /* "Pick one for me" */
    ui->overlay = OV_SORT;
}

static void draw_sort(ShUi *ui, const ShSurf *s)
{
    const char *rows[SORT_COUNT + 1], *vals[SORT_COUNT + 1];
    for (int i = 0; i < ui->nsort; i++) {
        int o = ui->sort_opts[i];
        rows[i] = o == SORT_COUNT ? "Pick one for me" : g_sort_label[o];
        vals[i] = o == SORT_COUNT ? "?" : o == ui->cfg->sort[ui->tab] ? "*" : "";
    }
    static const char *const tabn[4] = { "SORT  Home", "SORT  All Games", "SORT  Completed", "SORT  Favourites" };
    popup(s, tabn[ui->tab], rows, vals, ui->nsort, ui->ov_sel, 232, "Each tab keeps its own");
    static const Key k[] = { { "X", "Choose" }, { "O", "Cancel" } };
    keybar(s, k, 2);
}

/* ---- All: the menu on Triangle. Sort and "Pick one for me" on top, then every category
 * with the number of games in it. ---------------------------------------------------- */
#define CAT_ROWS 11                              /* rows on screen */
static const int g_all_sorts[] = { SORT_LAST, SORT_NAME, SORT_TIME, SORT_EMBLEM, SORT_ADDED };

static void open_cats(ShUi *ui)
{
    int t = ui->tab, cur = *tab_cat(ui, t);
    /* All: every category. Favourites: All Games and the categories that hold a favourite. */
    ui->menu_n = 0;
    for (int c = 0; c < ui->ncats; c++) {
        int n = ui->cat_count[c];
        if (t == TAB_FAV) {
            n = 0;
            for (int g = 0; g < ui->lib->n; g++) n += shown_type(ui, g) && ui->info[g].fav && in_category(ui, g, c);
            if (!n && c != 0 && c != cur) continue;
        }
        ui->menu_cat[ui->menu_n] = c; ui->menu_cnt[ui->menu_n++] = n;
    }
    ui->ov_sel = 2;
    for (int k = 0; k < ui->menu_n; k++) if (ui->menu_cat[k] == cur) ui->ov_sel = 2 + k;
    if (ui->menu_on_pick) ui->ov_sel = 1;
    ui->ov_top = 0;
    ui->overlay = OV_CATS;
}

static void draw_cats(ShUi *ui, const ShSurf *s)
{
    int t = ui->tab, cur_cat = *tab_cat(ui, t);
    int n = 2 + ui->menu_n, shown = n < CAT_ROWS ? n : CAT_ROWS;
    if (ui->ov_sel < ui->ov_top) ui->ov_top = ui->ov_sel;
    if (ui->ov_sel >= ui->ov_top + shown) ui->ov_top = ui->ov_sel - shown + 1;
    int w = 264, h = 30 + shown * 16 + 4;
    int x0 = (SCR_W - w) / 2, y0 = (SCR_H - 16 - h) / 2;
    gfx_dim(s, 0, 0, SCR_W, SCR_H, 35);
    gfx_rect(s, x0, y0, w, h, C_PANEL);
    gfx_frame(s, x0, y0, w, h, C_AMBER, 1);
    gfx_text(s, x0 + 10, y0 + 8, t == TAB_FAV ? "FAVOURITES" : "CATEGORY", C_AMBER);
    gfx_hline(s, x0 + 6, y0 + 20, w - 13, C_LINE);
    for (int r = 0; r < shown; r++) {
        int i = ui->ov_top + r, y = y0 + 26 + r * 16, on = i == ui->ov_sel;
        char b[48];
        if (on) gfx_rect(s, x0 + 4, y - 3, w - 8, 14, C_SEL);
        if (i == 0) {
            gfx_text(s, x0 + 12, y, "Sort", on ? C_WHITE : C_GREY);
            sh_snprintf(b, sizeof(b), on ? "< %s >" : "%s", g_sort_label[ui->cfg->sort[t]]);
            gfx_text_r(s, x0 + w - 12, y, b, on ? C_AMBER : C_GREY);
        } else if (i == 1) {
            gfx_text(s, x0 + 12, y, "Pick one for me", on ? C_WHITE : C_GREY);
            gfx_text_r(s, x0 + w - 12, y, "?", on ? C_AMBER : C_GREY);
        } else {
            int c = ui->menu_cat[i - 2], cur = c == cur_cat;
            if (cur) gfx_dot(s, x0 + 12, y + 1, C_AMBER, 1);
            char nm[40];
            cut(ui->cat_name[c], 22, nm);
            gfx_text(s, x0 + 24, y, nm, cur ? C_AMBER : on ? C_WHITE : C_GREY);
            sh_snprintf(b, sizeof(b), "%d", ui->menu_cnt[i - 2]);
            gfx_text_r(s, x0 + w - 12, y, b, on ? C_WHITE : C_DIM);
        }
        if (i == 1 && r < shown - 1) gfx_hline(s, x0 + 6, y + 12, w - 13, C_LINE);
    }
    if (ui->ov_top > 0) gfx_button(s, x0 + w / 2 - 4, y0 + 19, 'U');
    if (ui->ov_top + shown < n) gfx_button(s, x0 + w / 2 - 4, y0 + h - 7, 'D');
    static const Key k[] = { { "X", "Choose" }, { "LEFT/RIGHT", "Sort" }, { "O", "Close" } };
    keybar(s, k, 3);
}

/* ---- the PS button: a short menu of the things you leave Shelf for ------------------ */
enum { QK_USB, QK_SETTINGS, QK_RESTART, QK_SHUTDOWN, QK_SLEEP, QK_XMB, QK_COUNT };
static const char *const g_quick[QK_COUNT] = { "USB connection", "Settings", "Restart", "Shut down", "Sleep", "Exit to XMB" };

static void draw_quick(ShUi *ui, const ShSurf *s)
{
    char b[64];
    int w = 264, h = 30 + QK_COUNT * 16 + 4 + (ui->up_hide ? 0 : 36);
    int x0 = (SCR_W - w) / 2, y0 = (SCR_H - 16 - h) / 2;
    gfx_dim(s, 0, 0, SCR_W, SCR_H, 35);
    gfx_rect(s, x0, y0, w, h, C_PANEL);
    gfx_frame(s, x0, y0, w, h, C_AMBER, 1);
    gfx_text(s, x0 + 10, y0 + 8, "MENU", C_AMBER);
    gfx_text_r(s, x0 + w - 10, y0 + 8, ui->clock, C_DIM);
    gfx_hline(s, x0 + 6, y0 + 20, w - 13, C_LINE);
    for (int i = 0; i < QK_COUNT; i++) {
        int y = y0 + 26 + i * 16, on = i == ui->ov_sel;
        if (on) gfx_rect(s, x0 + 4, y - 3, w - 8, 14, C_SEL);
        gfx_text(s, x0 + 12, y, g_quick[i], on ? C_WHITE : C_GREY);
    }
    /* since the PSP was powered on (left out when Runtime's record cannot be read) */
    if (!ui->up_hide) {
        int y = y0 + 26 + QK_COUNT * 16 + 2;
        gfx_hline(s, x0 + 6, y - 2, w - 13, C_LINE);
        if (ui->up_sleep) sh_snprintf(b, sizeof(b), "%dh %02dm  +%dh %02dm asleep", (int)(ui->up_mins / 60), (int)(ui->up_mins % 60),
                                      (int)(ui->up_sleep / 60), (int)(ui->up_sleep % 60));
        else sh_snprintf(b, sizeof(b), "%dh %02dm", (int)(ui->up_mins / 60), (int)(ui->up_mins % 60));
        gfx_text(s, x0 + 12, y + 6, "On for", C_DIM);
        gfx_text_r(s, x0 + w - 12, y + 6, b, C_WHITE);
        gfx_text(s, x0 + 12, y + 20, "Battery", C_DIM);
        if (ui->up_pct0 >= 0 && ui->up_pct >= 0) {
            int d = ui->up_pct - ui->up_pct0;
            sh_snprintf(b, sizeof(b), "%d%% > %d%%  (%s%d%%)", ui->up_pct0, ui->up_pct, d > 0 ? "+" : d < 0 ? "-" : "", d < 0 ? -d : d);
            gfx_text_r(s, x0 + w - 12, y + 20, b, d < 0 ? C_GOLD : C_GREEN);
        } else gfx_text_r(s, x0 + w - 12, y + 20, "no reading", C_DIM);
    }
    static const Key k[] = { { "X", "Choose" }, { "O", "Close" } };
    keybar(s, k, 2);
}

/* game options: built each time */
enum { GO_FINISH, GO_FAV, GO_HIDE, GO_PIC, GO_INFO, GO_CANCEL, GO_FROM };
#define GO_MAX 7
static int g_go[GO_MAX], g_ngo;
static void game_rows(ShUi *ui, int g, const char **rows, const char **vals, char *picv)
{
    ShInfo *in = &ui->info[g];
    g_ngo = 0;
    /* start from (a game with a Sidequest slot), favourite, completed, journey, hide, picture, cancel */
    if (in->sq_newest >= 0 && ui->lib->g[g].type != GT_PS1) { g_go[g_ngo] = GO_FROM; rows[g_ngo] = "Start from..."; vals[g_ngo++] = ">"; }
    if (tab_on(ui, TAB_FAV)) { g_go[g_ngo] = GO_FAV; rows[g_ngo] = in->fav ? "Remove from Favourites" : "Add to Favourites"; vals[g_ngo++] = ""; }
    g_go[g_ngo] = GO_FINISH; rows[g_ngo] = in->finished ? "Mark as not completed" : "Mark as completed"; vals[g_ngo++] = "";
    g_go[g_ngo] = GO_INFO; rows[g_ngo] = "Journey & Emblems"; vals[g_ngo++] = "";
    if (!in->finished && tab_on(ui, 0)) { g_go[g_ngo] = GO_HIDE; rows[g_ngo] = in->hidden ? "Show on Home" : "Hide from Home"; vals[g_ngo++] = ""; }
    if (in->sq_dir[0]) {
        if (in->pic < 0) sh_strcpy(picv, 24, "< Newest >");
        else if (in->pic == 0) sh_strcpy(picv, 24, "< Suspend >");
        else if (in->pic == 6) sh_strcpy(picv, 24, "< Icon >");
        else sh_snprintf(picv, 24, "< State %d >", in->pic);
        g_go[g_ngo] = GO_PIC; rows[g_ngo] = "Picture"; vals[g_ngo++] = picv;
    }
    g_go[g_ngo] = GO_CANCEL; rows[g_ngo] = "Cancel"; vals[g_ngo++] = "";
}

/* ---- "Start from...": the game's slots, newest first, then "Start without loading" ---- */
static void open_start(ShUi *ui, int g)
{
    ShInfo *in = &ui->info[g];
    int n = 0;
    for (int k = 0; k < 6; k++) if (in->sq_has[k]) ui->start_slot[n++] = k;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++)
        if (in->sq_time[ui->start_slot[b]] > in->sq_time[ui->start_slot[a]]) { int t = ui->start_slot[a]; ui->start_slot[a] = ui->start_slot[b]; ui->start_slot[b] = t; }
    ui->start_slot[n++] = -1;
    ui->start_n = n;
    ui->ov_sel = 0;
    ui->overlay = OV_START;
}

static void draw_start(ShUi *ui, const ShSurf *s)
{
    int g = ui_selected_game(ui);
    if (g < 0) return;
    ShInfo *in = &ui->info[g];
    int n = ui->start_n, cur = ui->ov_sel;
    int w = 452, h = 30 + 118 + 20, x0 = (SCR_W - w) / 2, y0 = (SCR_H - 16 - h) / 2;
    gfx_dim(s, 0, 0, SCR_W, SCR_H, 35);
    gfx_rect(s, x0, y0, w, h, C_PANEL);
    gfx_frame(s, x0, y0, w, h, C_AMBER, 1);
    char head[64], nm[40], b[48];
    cut(ui_title(ui, g), 38, nm);
    sh_snprintf(head, sizeof(head), "START FROM  %s", nm);
    gfx_text(s, x0 + 10, y0 + 8, head, C_AMBER);
    gfx_hline(s, x0 + 6, y0 + 20, w - 13, C_LINE);
    int lw = w - 208 - 30, y = y0 + 28;
    for (int i = 0; i < n; i++) {
        int k = ui->start_slot[i];
        if (i == n - 1) { gfx_hline(s, x0 + 6, y - 2, lw + 4, C_LINE); y += 5; }
        if (i == cur) gfx_rect(s, x0 + 4, y - 3, lw + 8, 14, C_SEL);
        if (k < 0) gfx_text(s, x0 + 12, y, "Start without loading", i == cur ? C_WHITE : C_GREY);
        else {
            if (k == 0) sh_strcpy(b, sizeof(b), "Suspend"); else sh_snprintf(b, sizeof(b), "State %d", k);
            gfx_text(s, x0 + 12, y, b, i == cur ? C_WHITE : C_GREY);
            sh_format_date(in->sq_time[k], b, sizeof(b), 1);
            gfx_text_r(s, x0 + 8 + lw, y, strlen(b) > 4 ? b + 4 : b, i == cur ? C_AMBER : C_GREY);     /* "Nov 23 21:40" */
        }
        y += 16;
    }
    /* the picture of the row under the cursor, read again only when the row changes */
    static u16 pic[PIC_W * PIC_H];
    static int pic_g = -1, pic_k = -2, pic_ok;
    int px = x0 + w - 208 - 10, py = y0 + 28, k = ui->start_slot[cur];
    if (pic_g != g || pic_k != k) {
        char p[160];
        pic_g = g; pic_k = k; pic_ok = 0;
        if (k >= 0) { data_thm_path(in, k, p, sizeof(p)); pic_ok = img_read_thm(p, pic, PIC_W, PIC_H) == 0; }
    }
    gfx_rect(s, px, py, 208, 118, C_BLACK);
    if (k < 0) gfx_text_c(s, px + 104, py + 54, "from the beginning", C_DIM);
    else if (pic_ok) gfx_blit565_scaled(s, px, py, 208, 118, pic, PIC_W, PIC_H);
    else gfx_text_c(s, px + 104, py + 54, "no picture", C_DIM);
    gfx_frame(s, px - 1, py - 1, 210, 120, C_LINE, 1);
    gfx_text(s, x0 + 12, y0 + h - 14, "X on Home still goes straight to the newest.", C_DIM);
    static const Key keys[] = { { "X", "Start" }, { "O", "Back" } };
    keybar(s, keys, 2);
}

static void draw_game(ShUi *ui, const ShSurf *s)
{
    int g = ui_selected_game(ui);
    if (g < 0) return;
    const char *rows[GO_MAX], *vals[GO_MAX];
    char picv[24];
    game_rows(ui, g, rows, vals, picv);
    if (ui->ov_sel >= g_ngo) ui->ov_sel = g_ngo - 1;
    char head[48], nm[40];
    cut(ui_title(ui, g), 23, nm);
    sh_snprintf(head, sizeof(head), "OPTIONS  %s", nm);
    popup(s, head, rows, vals, g_ngo, ui->ov_sel, 280, ui->info[g].sq_dir[0] ? "Newest, Suspend, States, Icon" : NULL);
    static const Key k[] = { { "X", "Choose" }, { "LEFT/RIGHT", "Change" }, { "O", "Back" } };
    keybar(s, k, 3);
}

static void cycle_pic(ShInfo *in, int dir)
{
    /* order: newest (-1), suspend, states 1-5, icon (6); skip slots that do not exist */
    int v = in->pic;
    for (int tries = 0; tries < 9; tries++) {
        v += dir;
        if (v > 6) v = -1;
        if (v < -1) v = 6;
        if (v == -1 || v == 6 || in->sq_has[v]) break;
    }
    in->pic = (s8)v;
}

/* ---- settings --------------------------------------------------------------------- */
enum { IT_OPEN_TAB = 1, IT_GRID, IT_PICMODE, IT_TITLES, IT_PSN, IT_HB, IT_PS1, IT_HIDDEN, IT_BOOT, IT_ASKEXIT, IT_RETURN, IT_CAT_ALL, IT_CAT_FAV, IT_RESCAN, IT_REBUILD,
       IT_ARK, IT_PLUGIN, IT_USB, IT_SLEEP, IT_RESTART, IT_SHUTDOWN, IT_XMB, IT_RECOVERY, IT_INFO, IT_TAB_HOME, IT_TAB_FAV, IT_TAB_FIN };
enum { SEC_SHELF = 0, SEC_LIBRARY, SEC_ARK, SEC_PLUGINS, SEC_POWER, SEC_COUNT };
static const char *const g_sections[SEC_COUNT] = { "Shelf", "Library", "ARK-5", "Plugins", "Power & USB" };

static UiItem *add_item(ShUi *ui, int kind, const char *label, const char *value, int id, int arg, const char *desc)
{
    if (ui->nitems >= MAX_ITEMS) return &ui->items[MAX_ITEMS - 1];
    UiItem *it = &ui->items[ui->nitems++];
    it->kind = kind; it->id = id; it->arg = arg; it->desc = desc;
    sh_strcpy(it->label, sizeof(it->label), label);
    sh_strcpy(it->value, sizeof(it->value), value ? value : "");
    return it;
}

static const char *onoff(int v) { return v ? "On" : "Off"; }

static void build_items(ShUi *ui)
{
    char b[48];
    ui->nitems = 0;
    if (ui->set_sec == SEC_SHELF) {
        static const char *const tabs[5] = { "Home", SH_TAB_ALL, "Completed", "Where I left off", "Favourites" };
        ShConfig *c = ui->cfg;
        add_item(ui, 0, "Open on", tabs[c->open_tab >= 0 && c->open_tab <= 4 ? c->open_tab : 3], IT_OPEN_TAB, 0,
                 c->open_tab == 3 ? "Back to the tab, category and game you were on." : "The tab Shelf shows when it starts.");
        add_item(ui, 0, SH_TAB_ALL " opens on", ui->cat_name[cat_named(ui, c->all_default)], IT_CAT_ALL, 0, "The category the " SH_TAB_ALL " tab shows at every start.");
        add_item(ui, 0, "Favourites open on", ui->cat_name[cat_named(ui, c->fav_default)], IT_CAT_FAV, 0, "The category the Favourites tab shows at every start.");
        static const char *const grids[4] = { "3  large", "4", "5", "6  small" };
        add_item(ui, 0, SH_TAB_ALL ": games per row", grids[(c->grid_cols >= 3 && c->grid_cols <= 6 ? c->grid_cols : 6) - 3], IT_GRID, 0,
                 c->grid_cols == 3 ? "2 rows, with names under the icons." : c->grid_cols == 6 ? "4 rows of small icons." : "3 rows.");
        add_item(ui, 0, "Home picture", c->pic_mode ? "Game icon" : "Newest slot", IT_PICMODE, 0, "Sidequest's newest suspend/state picture, or the game's icon.");
        add_item(ui, 0, "Game names", c->titles_from_file ? "File name" : "From the game", IT_TITLES, 0, "Japanese titles always use the file name.");
        add_item(ui, 0, "Show PSN Games", onoff(c->show_psn), IT_PSN, 0, "EBOOT.PBP games with category EG.");
        add_item(ui, 0, "Show homebrew", onoff(c->show_homebrew), IT_HB, 0, "Apps and ports in PSP/GAME.");
        add_item(ui, 0, "Show PS1 Games", onoff(c->show_ps1), IT_PS1, 0, "PS1 EBOOT.PBP games.");
        add_item(ui, 0, "Show hidden on Home", onoff(c->show_hidden), IT_HIDDEN, 0, "Games hidden with SELECT > Hide from Home.");
        add_item(ui, 0, "Start Shelf at power on", onoff(ui->launcher_on), IT_BOOT, 0,
                 "The PSP powers on into Shelf, not the XMB.");
        add_item(ui, 0, "After a game", c->return_to_shelf ? "Back to Shelf" : "XMB", IT_RETURN, 0,
                 "Quitting a game: back to Shelf, or to the XMB.");
        add_item(ui, 0, "Home tab", onoff(c->tab_home), IT_TAB_HOME, 0, "Off: no Home tab. Shelf opens on " SH_TAB_ALL ".");
        add_item(ui, 0, "Favourites tab", onoff(c->tab_fav), IT_TAB_FAV, 0, "Off: no Favourites tab. Your favourites are kept.");
        add_item(ui, 0, "Completed tab", onoff(c->tab_finished), IT_TAB_FIN, 0, "Off: no Completed tab. Completed marks are kept.");
    } else if (ui->set_sec == SEC_LIBRARY) {
        add_item(ui, 1, "Rescan library", "", IT_RESCAN, 0, "Looks for new, changed and removed games.");
        add_item(ui, 1, "Rebuild pictures", "", IT_REBUILD, 0, "Reads every game's pictures again. Takes a while.");
        int cnt[4] = { 0, 0, 0, 0 }, games = 0, started = 0, done = 0, favs = 0, got = 0, total = 0;
        u32 secs = 0;
        for (int g = 0; g < ui->lib->n; g++) {
            if (ui->lib->g[g].type == GT_SKIP) continue;
            const ShInfo *in = &ui->info[g];
            cnt[ui->lib->g[g].type & 3]++; games++;
            secs += in->cp_secs;
            started += in->playing || in->finished;
            done += in->finished != 0;
            favs += in->fav;
            if (in->em_total > 0) { got += in->em_got; total += in->em_total; }
        }
        sh_snprintf(b, sizeof(b), "%d ISO  %d PSN  %d HB  %d PS1", cnt[0], cnt[1], cnt[2], cnt[3]);
        add_item(ui, 2, "Games", b, IT_INFO, 0, "Pictures are cached in ms0:/SHELF/CACHE/");
        sh_snprintf(b, sizeof(b), "%d of %d  (%d completed)", started, games, done);
        add_item(ui, 2, "Started", b, IT_INFO, 0, "Games you have played, and those marked as completed.");
        sh_snprintf(b, sizeof(b), "%dh %02dm", (int)(secs / 3600), (int)(secs / 60 % 60));
        add_item(ui, 2, "Time played", b, IT_INFO, 0, "Added up from Checkpoint's play times.");
        sh_snprintf(b, sizeof(b), "%d / %d", got, total);
        add_item(ui, 2, "Emblems", b, IT_INFO, 0, "Across every game that has an Emblem file.");
        sh_snprintf(b, sizeof(b), "%d", favs);
        add_item(ui, 2, "Favourites", b, IT_INFO, 0, "SELECT on a game > Add to Favourites.");
        if (ui->ms_total_mb) {
            sh_snprintf(b, sizeof(b), "%d.%d GB free of %d.%d GB", (int)(ui->ms_free_mb / 1024), (int)(ui->ms_free_mb % 1024 * 10 / 1024),
                        (int)(ui->ms_total_mb / 1024), (int)(ui->ms_total_mb % 1024 * 10 / 1024));
            add_item(ui, 2, "Memory Stick", b, IT_INFO, 0, NULL);
        }
        add_item(ui, 2, "Shelf " SH_VERSION_STR, "built " __DATE__, IT_INFO, 0, "Build " SH_BUILD_STAMP);
    } else if (ui->set_sec == SEC_ARK) {
        if (!ui->ark) { add_item(ui, 2, "ARK-5 settings not found", "", IT_INFO, 0, NULL); return; }
        for (int o = 0; o < ARK_OPT_COUNT; o++) {
            if (o == ARK_LAUNCHER) continue;
            int v = ark_get(ui->ark, o);
            add_item(ui, 0, g_ark_opts[o].label, g_ark_opts[o].vals[v], IT_ARK, o, g_ark_opts[o].desc);
        }
    } else if (ui->set_sec == SEC_PLUGINS) {
        for (int i = 0; i < ui->nplugins; i++) {
            ShPlugin *p = &ui->plugins[i];
            add_item(ui, 0, p->name, onoff(p->on), IT_PLUGIN, i, p->where);
        }
        if (!ui->nplugins) add_item(ui, 2, "No plugins in PLUGINS.TXT", "", IT_INFO, 0, NULL);
    } else {
        add_item(ui, 1, "USB connection", "", IT_USB, 0, "Connect to a computer. O ends it.");
        add_item(ui, 1, "Sleep", "", IT_SLEEP, 0, NULL);
        add_item(ui, 1, "Restart", "", IT_RESTART, 0, NULL);
        add_item(ui, 1, "Shut down", "", IT_SHUTDOWN, 0, NULL);
        add_item(ui, 1, "Exit to XMB", "", IT_XMB, 0, ui->launcher_on ? "Start-at-power-on resumes the next time you open Shelf." : NULL);
        add_item(ui, 1, "ARK recovery menu", "", IT_RECOVERY, 0, NULL);
    }
}

static void open_settings(ShUi *ui)
{
    static ShText *ark, *plug[2];
    char p[160];
    if (!ark) { ark = malloc(sizeof(ShText)); plug[0] = malloc(sizeof(ShText)); plug[1] = malloc(sizeof(ShText)); }
    ui->ark = NULL;
    if (ark && ui->has_ark) {
        sh_snprintf(p, sizeof(p), "%sSETTINGS.TXT", ui->ark_dir);
        SceIoStat st;
        memset(&st, 0, sizeof(st));
        if (sceIoGetstat(p, &st) >= 0 && tf_load(ark, p) == 0) ui->ark = ark;
    }
    ui->nplugins = 0;
    for (int f = 0; f < 2 && plug[0]; f++) {
        ui->plug[f] = NULL;
        if (f == 0) sh_strcpy(p, sizeof(p), "ms0:/SEPLUGINS/PLUGINS.TXT");
        else { if (!ui->has_ark) break; sh_snprintf(p, sizeof(p), "%sPLUGINS.TXT", ui->ark_dir); }
        SceIoStat st;
        memset(&st, 0, sizeof(st));
        if (sceIoGetstat(p, &st) < 0 || tf_load(plug[f], p) < 0) continue;
        ui->plug[f] = plug[f];
        int n = plugins_list(plug[f], ui->plugins + ui->nplugins, MAX_PLUGINS - ui->nplugins);
        for (int i = 0; i < n; i++) ui->plugin_file[ui->nplugins + i] = f;
        ui->nplugins += n;
    }
    ui->launcher_on = ui->has_ark && launcher_installed(ui->ark_dir) && ui->ark && ark_get(ui->ark, ARK_LAUNCHER);
    ui->ark_changed = 0;
    ui->set_sec = 0;
    ui->set_focus = 0;                           /* on the list of sections first */
    ui->ov_sel = 0; ui->ov_top = 0;
    build_items(ui);
    ui->overlay = OV_SETTINGS;
}

/* Leaving the settings: write what changed. Returns 1 if ARK's settings changed (restart to apply). */
static int close_settings(ShUi *ui)
{
    config_save(ui->cfg);
    int ark = ui->ark_changed;
    ui->ark_changed = 0;
    if (ui->ark && ui->ark->dirty) { tf_save(ui->ark); ark = 1; }
    for (int f = 0; f < 2; f++) if (ui->plug[f] && ui->plug[f]->dirty) { tf_save(ui->plug[f]); ark = 1; }
    return ark;
}

static void draw_settings(ShUi *ui, const ShSurf *s)
{
    gfx_dim(s, 0, 0, SCR_W, SCR_H, 35);
    gfx_rect(s, 10, 20, 460, 232, C_PANEL);
    gfx_frame(s, 10, 20, 460, 232, C_AMBER, 1);
    gfx_text(s, 20, 28, "SETTINGS", C_AMBER);
    int inside = ui->set_focus;                  /* 0: choosing a section; 1: changing what is in it */
    gfx_text_r(s, 460, 28, g_sections[ui->set_sec], C_DIM);
    gfx_hline(s, 16, 40, 448, C_LINE);
    for (int i = 0; i < SEC_COUNT; i++) {
        int y = 50 + i * 18, cur = i == ui->set_sec;
        if (cur) gfx_rect(s, 14, y - 4, 104, 16, C_SEL);
        if (cur && !inside) gfx_frame(s, 14, y - 4, 104, 16, C_AMBER, 1);
        gfx_text(s, 20, y, g_sections[i], cur ? (inside ? C_WHITE : C_AMBER) : C_GREY);
        if (cur && !inside) gfx_text(s, 108, y, ">", C_AMBER);
    }
    gfx_vline(s, 122, 46, 176, C_LINE);
    int rows = 10, y0 = 50;
    if (ui->ov_sel < ui->ov_top) ui->ov_top = ui->ov_sel;
    if (ui->ov_sel >= ui->ov_top + rows) ui->ov_top = ui->ov_sel - rows + 1;
    for (int r = 0; r < rows && ui->ov_top + r < ui->nitems; r++) {
        int i = ui->ov_top + r, y = y0 + r * 17;
        UiItem *it = &ui->items[i];
        int on = inside && i == ui->ov_sel;
        if (on) gfx_rect(s, 126, y - 4, 340, 16, C_SEL);
        char l[40];
        cut(it->label, 24, l);
        gfx_text(s, 132, y, l, it->kind == 2 ? C_DIM : on ? C_WHITE : C_GREY);
        if (it->kind == 0) {
            char v[48];
            sh_snprintf(v, sizeof(v), on ? "< %s >" : "%s", it->value);
            gfx_text_r(s, 460, y, v, on ? C_AMBER : C_GREY);
        } else if (it->kind == 1) { if (on) gfx_button(s, 452, y, 'X'); }
        else gfx_text_r(s, 460, y, it->value, C_DIM);
    }
    if (ui->ov_top > 0) gfx_button(s, 290, 43, 'U');
    if (ui->ov_top + rows < ui->nitems) gfx_button(s, 290, 222, 'D');
    gfx_hline(s, 16, 230, 448, C_LINE);
    static const char *const about[SEC_COUNT] = {
        "How Shelf looks and starts.", "Rescan, rebuild pictures, and your totals.", "ARK-5's own options (SETTINGS.TXT).",
        "Turn plugins on and off (PLUGINS.TXT).", "USB, sleep, restart, shut down, XMB, recovery." };
    const char *d = ui->ov_sel < ui->nitems ? ui->items[ui->ov_sel].desc : NULL;
    if (!inside) gfx_text_n(s, 20, 236, about[ui->set_sec], 55, C_DIM);
    else if (ui->set_sec == SEC_ARK && ui->ark) gfx_text_n(s, 20, 236, d && d[0] ? d : "ARK reads these at the next restart.", 55, C_DIM);
    else if (ui->set_sec == SEC_PLUGINS && ui->nplugins) { char b[80]; sh_snprintf(b, sizeof(b), "Runs in: %s. Applies at the next restart.", d ? d : ""); gfx_text_n(s, 20, 236, b, 55, C_DIM); }
    else if (d) gfx_text_n(s, 20, 236, d, 55, C_DIM);
    static const Key k_sec[] = { { "UP/DOWN", "Section" }, { "X", "Open" }, { "O", "Save and close" } };
    static const Key k_in[] = { { "UP/DOWN", "Move" }, { "LEFT/RIGHT", "Change" }, { "X", "Run" }, { "O", "Sections" } };
    if (inside) keybar(s, k_in, 4); else keybar(s, k_sec, 3);
}

static int settings_change(ShUi *ui, int dir, int press)
{
    if (ui->ov_sel >= ui->nitems) return ACT_NONE;
    UiItem *it = &ui->items[ui->ov_sel];
    ShConfig *c = ui->cfg;
    int relist = 0;
    switch (it->id) {
    case IT_OPEN_TAB: {                          /* in the order of the tabs, then "where I left off" */
        static const int order[5] = { 0, 1, 4, 2, 3 };
        int k = 0;
        for (int i = 0; i < 5; i++) if (order[i] == c->open_tab) k = i;
        for (int n = 0; n < 5; n++) {            /* tabs that are switched off are not offered */
            k = (k + 5 + (dir ? dir : 1)) % 5;
            int o = order[k];
            if (o == 3 || o == 1 || (o == 0 && c->tab_home) || (o == 4 && c->tab_fav) || (o == 2 && c->tab_finished)) break;
        }
        c->open_tab = order[k];
        break;
    }
    case IT_TAB_HOME:
    case IT_TAB_FAV:
    case IT_TAB_FIN: {
        int *v = it->id == IT_TAB_HOME ? &c->tab_home : it->id == IT_TAB_FAV ? &c->tab_fav : &c->tab_finished;
        int open = it->id == IT_TAB_HOME ? 0 : it->id == IT_TAB_FAV ? 4 : 2;
        *v ^= 1;
        if (!*v && c->open_tab == open) c->open_tab = 1;          /* "Open on" cannot name a tab that is off */
        if (!tab_on(ui, ui->tab)) ui->tab = 1;
        break;
    }
    case IT_CAT_ALL:
    case IT_CAT_FAV: {
        char *name = it->id == IT_CAT_ALL ? c->all_default : c->fav_default;
        int k = (cat_named(ui, name) + ui->ncats + (dir ? dir : 1)) % (ui->ncats ? ui->ncats : 1);
        sh_strcpy(name, 32, ui->cat_name[k]);
        *tab_cat(ui, it->id == IT_CAT_ALL ? 1 : TAB_FAV) = k;     /* and shown at once */
        build_lists(ui);
        break;
    }
    case IT_ASKEXIT:  c->confirm_exit ^= 1; break;
    case IT_RETURN:
        c->return_to_shelf ^= 1;
        break;
    case IT_GRID:     c->grid_cols = 3 + ((c->grid_cols - 3) + 4 + (dir ? dir : 1)) % 4; ui->top[1] = ui->top[TAB_FAV] = 0; break;
    case IT_PICMODE:  c->pic_mode ^= 1; g_hero_game = -1; for (int i = 0; i < SH_MAX_GAMES; i++) g_card_src[i] = -2; break;
    case IT_TITLES:   c->titles_from_file ^= 1; relist = 1; break;
    case IT_PSN:      c->show_psn ^= 1; relist = 1; break;
    case IT_HB:       c->show_homebrew ^= 1; relist = 1; break;
    case IT_PS1:      c->show_ps1 ^= 1; relist = 1; break;
    case IT_HIDDEN:   c->show_hidden ^= 1; relist = 1; break;
    case IT_ARK: {
        int n = g_ark_opts[it->arg].nvals, v = (ark_get(ui->ark, it->arg) + n + dir) % n;
        ark_set(ui->ark, it->arg, v);
        break;
    }
    case IT_PLUGIN: {
        ShPlugin *p = &ui->plugins[it->arg];
        p->on ^= 1;
        plugins_set(ui->plug[ui->plugin_file[it->arg]], p, p->on);
        break;
    }
    case IT_BOOT:
        if (!ui->has_ark) { ui_message(ui, "ARK-5 was not found.", NULL, NULL); return ACT_NONE; }
        ui->confirm_action = IT_BOOT;
        if (ui->launcher_on) { sh_strcpy(ui->msg[0], 64, "Stop starting Shelf at power on?"); sh_strcpy(ui->msg[1], 64, "The PSP will power on into the XMB."); }
        else { sh_strcpy(ui->msg[0], 64, "Start Shelf at power on?"); sh_strcpy(ui->msg[1], 64, "The PSP will power on into Shelf."); }
        ui->msg[2][0] = 0;
        ui->overlay = OV_CONFIRM;
        return ACT_NONE;
    default:
        if (!press) return ACT_NONE;
        switch (it->id) {
        case IT_RESCAN:   return ACT_RESCAN;
        case IT_REBUILD:  ui->confirm_action = IT_REBUILD; sh_strcpy(ui->msg[0], 64, "Read every game's pictures again?"); sh_strcpy(ui->msg[1], 64, "This takes a minute or two."); ui->msg[2][0] = 0; ui->overlay = OV_CONFIRM; return ACT_NONE;
        case IT_USB:      ui->usb_from_quick = 0; return ACT_USB;
        case IT_SLEEP:    return ACT_SLEEP;
        case IT_RESTART:  ui->confirm_action = IT_RESTART; sh_strcpy(ui->msg[0], 64, "Restart the PSP?"); ui->msg[1][0] = ui->msg[2][0] = 0; ui->overlay = OV_CONFIRM; return ACT_NONE;
        case IT_SHUTDOWN: ui->confirm_action = IT_SHUTDOWN; sh_strcpy(ui->msg[0], 64, "Shut the PSP down?"); ui->msg[1][0] = ui->msg[2][0] = 0; ui->overlay = OV_CONFIRM; return ACT_NONE;
        case IT_XMB:      return ACT_XMB;
        case IT_RECOVERY: ui->confirm_action = IT_RECOVERY; sh_strcpy(ui->msg[0], 64, "Open ARK's recovery menu?"); ui->msg[1][0] = ui->msg[2][0] = 0; ui->overlay = OV_CONFIRM; return ACT_NONE;
        }
        return ACT_NONE;
    }
    if (relist) { build_categories(ui); build_lists(ui); }
    build_items(ui);
    return ACT_NONE;
}

static void draw_confirm(ShUi *ui, const ShSurf *s, int is_confirm)
{
    int lines = 0;
    for (int i = 0; i < 3; i++) if (ui->msg[i][0]) lines = i + 1;
    int w = 0;
    for (int i = 0; i < 3; i++) if (gfx_tw(ui->msg[i]) > w) w = gfx_tw(ui->msg[i]);
    w += 40; if (w < 240) w = 240; if (w > 460) w = 460;
    int h = 36 + lines * 12, x0 = (SCR_W - w) / 2, y0 = (SCR_H - 16 - h) / 2;
    gfx_dim(s, 0, 0, SCR_W, SCR_H, 35);
    gfx_rect(s, x0, y0, w, h, C_PANEL);
    gfx_frame(s, x0, y0, w, h, C_AMBER, 1);
    for (int i = 0; i < lines; i++) gfx_text_n(s, x0 + 20, y0 + 12 + i * 12, ui->msg[i], (w - 40) / 8, i == 0 ? C_WHITE : C_GREY);
    if (is_confirm) { static const Key k[] = { { "X", "Yes" }, { "O", "No" } }; keybar(s, k, 2); }
    else { static const Key k[] = { { "X", "OK" } }; keybar(s, k, 1); }
}

static void draw_usb(ShUi *ui, const ShSurf *s)
{
    gfx_dim(s, 0, 0, SCR_W, SCR_H, 30);
    gfx_rect(s, 90, 80, 300, 90, C_PANEL);
    gfx_frame(s, 90, 80, 300, 90, C_AMBER, 1);
    gfx_text_c(s, SCR_W / 2, 96, "USB MODE", C_AMBER);
    gfx_text_c(s, SCR_W / 2, 120, ui->usb_state == 2 ? "Connected to the computer" : "Waiting for the computer", ui->usb_state == 2 ? C_GREEN : C_WHITE);
    gfx_text_c(s, SCR_W / 2, 140, "Eject it on the computer first", C_GREY);
    static const Key k[] = { { "O", "End USB mode" } };
    keybar(s, k, 1);
}

void ui_draw_progress(ShUi *ui, const ShSurf *s, const char *title, int done, int total, const char *name)
{
    (void)ui;
    gfx_rect(s, 0, 0, SCR_W, SCR_H, C_BG);
    gfx_text(s, 6, 4, "SHELF", C_AMBER);
    gfx_rect(s, 60, 96, 360, 80, C_PANEL);
    gfx_frame(s, 60, 96, 360, 80, C_AMBER, 1);
    gfx_text_c(s, SCR_W / 2, 108, title, C_WHITE);
    char b[64];
    if (total) { sh_snprintf(b, sizeof(b), "%d of %d", done, total); gfx_text_c(s, SCR_W / 2, 122, b, C_GREY); }
    gfx_bar(s, 80, 138, 320, 4, done, total ? total : 1, C_AMBER);
    char t[48];
    sh_tidy_name(name, b, sizeof(b));
    cut(b, 40, t);
    gfx_text_c(s, SCR_W / 2, 152, t, C_DIM);
}

void ui_draw_launch(ShUi *ui, const ShSurf *s, int g)
{
    static u16 back[PIC_W * PIC_H];
    ShInfo *in = &ui->info[g];
    /* This is the one place a game's picture fills the screen, and it is drawn once: read it
     * from the game at full size and in full colour. The cache's half-size copy is only the
     * way out if that cannot be done. */
    int full = 0;
    if (ui->lib->g[g].has_pic) {
        u32 plen = 0;
        u8 *png = scan_load_pic(&ui->lib->g[g], &plen);
        if (png) { full = img_png_fill32(png, plen, SCR_W, SCR_H, s->px, s->stride, 45) == 0; free(png); }
    }
    if (full) {}
    else if (ui->lib->g[g].has_pic && cache_read(ui->lib->g[g].key, PIC_BACK, back) == 0) {
        for (int y = 0; y < SCR_H; y++)
            for (int x = 0; x < SCR_W; x++) {
                u16 p = back[(y / 2) * PIC_W + x / 2];
                u32 r = (p & 31) << 3, gg = ((p >> 5) & 63) << 2, b = (p >> 11) << 3;
                s->px[y * s->stride + x] = RGB(r * 45 / 100, gg * 45 / 100, b * 45 / 100);
            }
    } else gfx_rect(s, 0, 0, SCR_W, SCR_H, C_BG);
    const u16 *ic = big_icon(ui, g);
    if (ic) { gfx_blit565(s, (SCR_W - ICON_W) / 2, 60, ic, ICON_W, ICON_H); gfx_frame(s, (SCR_W - ICON_W) / 2 - 1, 59, ICON_W + 2, ICON_H + 2, C_BLACK, 1); }
    else no_icon(ui, s, (SCR_W - ICON_W) / 2, 60, ICON_W, ICON_H, g);
    char t[64], b[64];
    cut(ui_title(ui, g), 50, t);
    gfx_text_c(s, SCR_W / 2, 154, t, C_WHITE);
    int rs = resume_slot(ui, g, ui->tab == 0);
    if (rs == 0) gfx_text_c(s, SCR_W / 2, 168, "Resuming suspended game", C_AMBER);
    else if (rs > 0) { sh_snprintf(b, sizeof(b), "Continuing from state %d", rs); gfx_text_c(s, SCR_W / 2, 168, b, C_AMBER); }
    else gfx_text_c(s, SCR_W / 2, 168, "Starting game", C_GREY);
    if (rs < 0 && !ui->act_from && in->sq_newest >= 0 && ui->lib->g[g].type != GT_PS1) {
        sh_format_date(in->sq_time[(int)in->sq_newest], t, sizeof(t), 1);
        sh_snprintf(b, sizeof(b), "Sidequest will offer: %s", t);
        gfx_text_c(s, SCR_W / 2, 182, b, C_GREY);
    }
    gfx_bar(s, 160, 206, 160, 3, 2, 5, C_AMBER);
}

void ui_draw(ShUi *ui, const ShSurf *s)
{
    gfx_rect(s, 0, 0, SCR_W, SCR_H, C_BG);
    titlebar(ui, s);
    if (ui->tab == 0) draw_playing(ui, s);
    else if (is_grid(ui->tab)) draw_all(ui, s);
    else draw_finished(ui, s);
    switch (ui->overlay) {
    case OV_INFO: draw_info(ui, s); break;
    case OV_SORT: draw_sort(ui, s); break;
    case OV_CATS: draw_cats(ui, s); break;
    case OV_QUICK: draw_quick(ui, s); break;
    case OV_GAME: draw_game(ui, s); break;
    case OV_START: draw_start(ui, s); break;
    case OV_SETTINGS: draw_settings(ui, s); break;
    case OV_CONFIRM: if (ui->set_sec >= 0) draw_settings(ui, s); draw_confirm(ui, s, 1); break;
    case OV_MESSAGE: if (ui->confirm_action == -1) draw_settings(ui, s); draw_confirm(ui, s, 0); break;
    case OV_USB: draw_usb(ui, s); break;
    }
    ui->dirty = 0;
}

/* ---- input ------------------------------------------------------------------------- */
static void move_sel(ShUi *ui, int d)
{
    int t = ui->tab, n = ui->count[t];
    if (!n) return;
    int v = ui->sel[t] + d;
    if (v < 0) v = 0;
    if (v >= n) v = n - 1;
    ui->sel[t] = v;
}

static u32 rnd(ShUi *ui)
{
    ui->rng = ui->rng * 1664525u + 1013904223u;
    return ui->rng >> 8;
}

/* A game from this list, one not yet started if there is any. */
static void pick_random(ShUi *ui, int t)
{
    int n = ui->count[t], fresh = 0;
    if (!n) return;
    for (int i = 0; i < n; i++) fresh += !ui->info[ui->list[t][i]].playing && !ui->info[ui->list[t][i]].finished;
    int want = (int)(rnd(ui) % (u32)(fresh ? fresh : n));
    for (int i = 0; i < n; i++) {
        const ShInfo *in = &ui->info[ui->list[t][i]];
        if (fresh && (in->playing || in->finished)) continue;
        if (want-- == 0) { ui->sel[t] = i; break; }
    }
}

static int home_input(ShUi *ui, u32 b)
{
    int t = ui->tab;
    if (b & (B_L | B_R)) {                       /* Playing, All, Favourites, Finished */
        static const int next[4] = { 1, TAB_FAV, 0, 2 }, prev[4] = { 2, 0, TAB_FAV, 1 };
        do ui->tab = (b & B_R) ? next[ui->tab] : prev[ui->tab]; while (!tab_on(ui, ui->tab));
        return ACT_REDRAW;
    }
    if (is_grid(t)) {                            /* the grid: up and down by a row */
        int cols = grid_layout(ui)->cols, n = ui->count[t], v = ui->sel[t];
        if ((b & B_UP) && v - cols >= 0) v -= cols;
        if ((b & B_DOWN) && n) { if (v + cols < n) v += cols; else if (v / cols < (n - 1) / cols) v = n - 1; }
        ui->sel[t] = v;
    } else if (t == 2) {
        if (b & B_UP) move_sel(ui, -1);
        if (b & B_DOWN) move_sel(ui, 1);
    }
    if (b & B_LEFT) move_sel(ui, -1);
    if (b & B_RIGHT) move_sel(ui, 1);
    if (b & B_START) { open_settings(ui); return ACT_REDRAW; }
    if (b & B_HOME) { ui->ov_sel = 0; ui->overlay = OV_QUICK; return ACT_REDRAW; }
    if ((b & B_CIRCLE) && t != 0) {              /* O: back to Home, or to All when Home is switched off */
        if (tab_on(ui, 0)) ui->tab = 0; else ui->tab = 1;
        return ACT_REDRAW;
    }
    if (b & B_TRI) { if (is_grid(t)) open_cats(ui); else open_sort(ui); return ACT_REDRAW; }
    int g = ui_selected_game(ui);
    if (g < 0) return ACT_REDRAW;
    if (b & B_CROSS) { ui->act_game = g; return ACT_LAUNCH; }
    if (b & B_SQUARE) open_info(ui);
    if (b & B_SELECT) { ui->ov_sel = 0; ui->overlay = OV_GAME; }
    return ACT_REDRAW;
}

/* The stick does what the d-pad does. */
static u32 stick_as_dpad(u32 b)
{
    if (b & B_AUP) b |= B_UP;
    if (b & B_ADOWN) b |= B_DOWN;
    if (b & B_ALEFT) b |= B_LEFT;
    if (b & B_ARIGHT) b |= B_RIGHT;
    return b & (0xFFFF | B_HOME);
}

int ui_input(ShUi *ui, u32 b)
{
    if (!b) return ACT_NONE;
    ui->dirty = 1;
    ui->rng += b * 2654435761u + ui->now;        /* presses stir the random pick */
    b = stick_as_dpad(b);
    if (ui->overlay == OV_NONE) return home_input(ui, b);
    switch (ui->overlay) {
    case OV_QUICK:
        if (b & (B_CIRCLE | B_HOME)) ui->overlay = OV_NONE;
        else if (b & B_UP) ui->ov_sel = (ui->ov_sel + QK_COUNT - 1) % QK_COUNT;
        else if (b & B_DOWN) ui->ov_sel = (ui->ov_sel + 1) % QK_COUNT;
        else if (b & B_CROSS) {
            ui->overlay = OV_NONE;
            switch (ui->ov_sel) {
            case QK_USB:      ui->usb_from_quick = 1; return ACT_USB;
            case QK_SETTINGS: open_settings(ui); break;
            case QK_RESTART:  return ACT_RESTART;
            case QK_SHUTDOWN: return ACT_SHUTDOWN;
            case QK_SLEEP:    return ACT_SLEEP;
            case QK_XMB:      return ACT_XMB;
            default: break;
            }
        }
        return ACT_REDRAW;
    case OV_CATS: {
        int n = 2 + ui->menu_n, t = ui->tab;
        if (b & (B_CIRCLE | B_TRI)) ui->overlay = OV_NONE;
        else if (b & B_UP) ui->ov_sel = (ui->ov_sel + n - 1) % n;
        else if (b & B_DOWN) ui->ov_sel = (ui->ov_sel + 1) % n;
        else if (b & (B_LEFT | B_RIGHT | B_CROSS)) {
            if (ui->ov_sel == 0 || (b & (B_LEFT | B_RIGHT))) {          /* the order: stays open so you can see it change */
                int ns = (int)(sizeof(g_all_sorts) / sizeof(g_all_sorts[0])), k = 0;
                for (int i = 0; i < ns; i++) if (g_all_sorts[i] == ui->cfg->sort[t]) k = i;
                ui->cfg->sort[t] = g_all_sorts[(k + ((b & B_LEFT) ? ns - 1 : 1)) % ns];
                config_save(ui->cfg);
                build_lists(ui);
                ui->sel[t] = 0; ui->top[t] = 0;
            } else if (ui->ov_sel == 1) {
                pick_random(ui, t);
                ui->menu_on_pick = 1;
                ui->overlay = OV_NONE;
            } else {
                *tab_cat(ui, t) = ui->menu_cat[ui->ov_sel - 2];
                ui->menu_on_pick = 0;
                ui->count[t] = 0;
                build_lists(ui);
                ui->sel[t] = 0; ui->top[t] = 0;
                ui->overlay = OV_NONE;
            }
        }
        return ACT_REDRAW;
    }
    case OV_INFO:
        if (b & B_CIRCLE) ui->overlay = OV_NONE;
        else if (b & (B_L | B_R | B_LEFT | B_RIGHT)) { ui->ov_sel ^= 1; ui->ov_page = 0; }      /* Emblem <> journey */
        else if ((b & B_UP) && ui->ov_sel == 1) { if (ui->ov_page > 0) ui->ov_page--; }
        else if ((b & B_DOWN) && ui->ov_sel == 1) ui->ov_page++;
        else if (b & B_CROSS) { ui->overlay = OV_NONE; ui->act_game = ui_selected_game(ui); return ACT_LAUNCH; }
        return ACT_REDRAW;
    case OV_SORT:
        if (b & B_CIRCLE) ui->overlay = OV_NONE;
        else if (b & B_UP) ui->ov_sel = (ui->ov_sel + ui->nsort - 1) % ui->nsort;
        else if (b & B_DOWN) ui->ov_sel = (ui->ov_sel + 1) % ui->nsort;
        else if ((b & B_CROSS) && ui->sort_opts[ui->ov_sel] == SORT_COUNT) {
            pick_random(ui, ui->tab);
            ui->overlay = OV_NONE;
        }
        else if (b & B_CROSS) {
            ui->cfg->sort[ui->tab] = ui->sort_opts[ui->ov_sel];
            config_save(ui->cfg);
            build_lists(ui);
            ui->sel[ui->tab] = 0; ui->top[ui->tab] = 0;
            ui->overlay = OV_NONE;
        }
        return ACT_REDRAW;
    case OV_START: {
        int g = ui_selected_game(ui);
        if (g < 0 || ui->start_n < 1) { ui->overlay = OV_NONE; return ACT_REDRAW; }
        if (b & B_CIRCLE) { ui->ov_sel = 0; ui->overlay = OV_GAME; return ACT_REDRAW; }      /* back to Options, on the row that led here */
        if (b & (B_UP | B_AUP)) ui->ov_sel = (ui->ov_sel + ui->start_n - 1) % ui->start_n;
        if (b & (B_DOWN | B_ADOWN)) ui->ov_sel = (ui->ov_sel + 1) % ui->start_n;
        if (b & B_CROSS) {
            int k = ui->start_slot[ui->ov_sel];
            ui->overlay = OV_NONE;
            ui->act_game = g; ui->act_from = k >= 0 ? 1 : 2; ui->act_slot = k >= 0 ? k : 0;
            return ACT_LAUNCH;
        }
        return ACT_REDRAW;
    }
    case OV_GAME: {
        int g = ui_selected_game(ui);
        if (g < 0 || (b & B_CIRCLE)) { ui->overlay = OV_NONE; return ACT_REDRAW; }
        const char *rows[GO_MAX], *vals[GO_MAX];
        char picv[24];
        game_rows(ui, g, rows, vals, picv);
        ShInfo *in = &ui->info[g];
        if (b & B_UP) ui->ov_sel = (ui->ov_sel + g_ngo - 1) % g_ngo;
        else if (b & B_DOWN) ui->ov_sel = (ui->ov_sel + 1) % g_ngo;
        else if ((b & (B_LEFT | B_RIGHT)) && g_go[ui->ov_sel] == GO_PIC) {
            cycle_pic(in, (b & B_RIGHT) ? 1 : -1);
            state_save(ui->lib, ui->info);
        } else if (b & B_CROSS) {
            switch (g_go[ui->ov_sel]) {
            case GO_FINISH:
                in->finished = in->finished ? 0 : (ui->now ? ui->now : 1);
                data_finish(&ui->lib->g[g], in);
                state_save(ui->lib, ui->info);
                build_lists(ui);
                ui->overlay = OV_NONE;
                break;
            case GO_FAV:
                in->fav ^= 1;
                state_save(ui->lib, ui->info);
                build_categories(ui);
                build_lists(ui);
                ui->overlay = OV_NONE;
                break;
            case GO_HIDE:
                in->hidden ^= 1;
                state_save(ui->lib, ui->info);
                build_lists(ui);
                ui->overlay = OV_NONE;
                break;
            case GO_PIC: cycle_pic(in, 1); state_save(ui->lib, ui->info); break;
            case GO_INFO: open_info(ui); break;
            case GO_FROM: open_start(ui, g); break;
            default: ui->overlay = OV_NONE; break;
            }
        }
        return ACT_REDRAW;
    }
    case OV_SETTINGS:
        if (ui->set_focus && (b & B_CIRCLE)) { ui->set_focus = 0; return ACT_REDRAW; }      /* back to the sections */
        if (!ui->set_focus && !(b & B_CIRCLE)) {
            int d = (b & (B_DOWN | B_R)) ? 1 : (b & (B_UP | B_L)) ? SEC_COUNT - 1 : 0;
            if (d) { ui->set_sec = (ui->set_sec + d) % SEC_COUNT; ui->ov_sel = 0; ui->ov_top = 0; build_items(ui); }
            else if ((b & (B_CROSS | B_RIGHT)) && ui->nitems) {                         /* into the section */
                ui->set_focus = 1;
                ui->ov_sel = 0;
                while (ui->ov_sel < ui->nitems - 1 && ui->items[ui->ov_sel].kind == 2) ui->ov_sel++;
            }
            return ACT_REDRAW;
        }
        if (b & B_CIRCLE) {
            ui->overlay = OV_NONE;
            if (close_settings(ui)) {
                ui->confirm_action = IT_RESTART;
                sh_strcpy(ui->msg[0], 64, "ARK settings saved.");
                sh_strcpy(ui->msg[1], 64, "Restart now to use them?");
                ui->msg[2][0] = 0;
                ui->overlay = OV_CONFIRM;
                ui->set_sec = -1;           /* no settings panel behind this question */
            }
            return ACT_REDRAW;
        }
        if (b & (B_L | B_R)) { ui->set_sec = (ui->set_sec + ((b & B_R) ? 1 : SEC_COUNT - 1)) % SEC_COUNT; ui->ov_sel = 0; ui->ov_top = 0; build_items(ui); return ACT_REDRAW; }
        if (b & B_UP) { if (ui->nitems) ui->ov_sel = (ui->ov_sel + ui->nitems - 1) % ui->nitems; }
        else if (b & B_DOWN) { if (ui->nitems) ui->ov_sel = (ui->ov_sel + 1) % ui->nitems; }
        else if (b & B_LEFT) return settings_change(ui, -1, 0);
        else if (b & B_RIGHT) return settings_change(ui, 1, 0);
        else if (b & B_CROSS) return settings_change(ui, 1, 1);
        return ACT_REDRAW;
    case OV_CONFIRM: {
        int back = ui->set_sec >= 0 ? OV_SETTINGS : OV_NONE;
        if (ui->set_sec < 0) ui->set_sec = 0;
        if (b & B_CIRCLE) { ui->overlay = back; return ACT_REDRAW; }
        if (!(b & B_CROSS)) return ACT_NONE;
        ui->overlay = back;
        switch (ui->confirm_action) {
        case IT_RESTART: return ACT_RESTART;
        case IT_SHUTDOWN: return ACT_SHUTDOWN;
        case IT_RECOVERY: return ACT_RECOVERY;
        case IT_REBUILD: return ACT_REBUILD;
        case IT_XMB: return ACT_XMB;
        case IT_BOOT: {
            char m[64];
            /* ARK changes made on this visit go to the file first: the launcher line is
             * written there next, and the copy held here is then read again */
            if (ui->ark && ui->ark->dirty && tf_save(ui->ark) == 0) ui->ark_changed = 1;
            if (ui->launcher_on) launcher_remove(ui->ark_dir, m, sizeof(m));
            else launcher_install(ui->ark_dir, ui->self, m, sizeof(m));
            char p[160];
            sh_snprintf(p, sizeof(p), "%sSETTINGS.TXT", ui->ark_dir);
            if (ui->ark && !ui->ark->dirty) tf_load(ui->ark, p);
            ui->launcher_on = launcher_installed(ui->ark_dir) && ui->ark && ark_get(ui->ark, ARK_LAUNCHER);
            build_items(ui);
            ui_message(ui, m, NULL, NULL);
            ui->overlay = OV_MESSAGE;
            ui->set_sec = SEC_SHELF;
            ui->confirm_action = -1;          /* the message returns to the settings */
            return ACT_REDRAW;
        }
        }
        return ACT_REDRAW;
    }
    case OV_MESSAGE:
        if (b & (B_CROSS | B_CIRCLE)) ui->overlay = ui->confirm_action == -1 ? OV_SETTINGS : OV_NONE;
        if (ui->overlay != OV_MESSAGE) ui->confirm_action = 0;
        return ACT_REDRAW;
    case OV_USB:
        return (b & B_CIRCLE) ? ACT_USB : ACT_NONE;   /* the main loop ends USB mode */
    }
    return ACT_NONE;
}
