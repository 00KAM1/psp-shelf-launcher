/*
 * Shelf - finding the games and keeping their pictures.
 *
 * Shown: PSP games as ISO/CSO/ZSO/DAX under ms0:/ISO, and EBOOT.PBP games under
 * ms0:/PSP/GAME: homebrew (category MG), PSN games (EG) and PS1 games (ME).
 * Sub-folders are followed (ISO three levels, PSP/GAME three levels), so
 * PSP/GAME/PORTS/ZeldaPSP and ISO/PSP/... are found. A folder that holds an
 * EBOOT.PBP is a game and is not looked into any further.
 * Not shown: DLC and update folders (they have no EBOOT.PBP, or one whose
 * category is not one of the three), 1.50 "%" folders and Shelf itself.
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "scan.h"
#include "iso.h"
#include "image.h"

#define LIB_MAGIC   0x42494C53u                  /* "SLIB" */
#define LIB_VERSION 1
#define CACHE_MAGIC 0x31434853u                  /* "SHC1" */
#define MAX_SFO     (64 * 1024)
#define MAX_PNG     (1024 * 1024)

typedef struct { u32 magic, version, entry, count; } LibHead;
typedef struct { u32 magic, flags; } CacheHead;

#define OFF_ICON  (sizeof(CacheHead))
#define OFF_SMALL (OFF_ICON + ICON_W * ICON_H * 2)
#define OFF_BACK  (OFF_SMALL + SMALL_W * SMALL_H * 2)
#define CACHE_SIZE (OFF_BACK + PIC_W * PIC_H * 2)

void cache_path(u32 key, char *out, int size) { sh_snprintf(out, size, SH_CACHE_DIR "/%08X.IMG", key); }

int cache_read(u32 key, int which, u16 *out)
{
    char p[64];
    cache_path(key, p, sizeof(p));
    SceUID fd = sceIoOpen(p, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    CacheHead h;
    int ok = sceIoRead(fd, &h, sizeof(h)) == (int)sizeof(h) && h.magic == CACHE_MAGIC;
    u32 off = which == PIC_ICON ? OFF_ICON : which == PIC_SMALL ? OFF_SMALL : OFF_BACK;
    int len = which == PIC_ICON ? ICON_W * ICON_H * 2 : which == PIC_SMALL ? SMALL_W * SMALL_H * 2 : PIC_W * PIC_H * 2;
    int bit = which == PIC_BACK ? 2 : 1;
    ok = ok && (h.flags & bit) && sceIoLseek32(fd, off, PSP_SEEK_SET) == (int)off && sceIoRead(fd, out, len) == len;
    sceIoClose(fd);
    return ok ? 0 : -1;
}

/* Writes the cache file from the PNGs found in the game (either may be missing). */
static void cache_write(ShGame *g, const u8 *icon, u32 ilen, const u8 *pic, u32 plen)
{
    static u8 buf[CACHE_SIZE];
    CacheHead *h = (CacheHead *)buf;
    memset(buf, 0, sizeof(buf));
    h->magic = CACHE_MAGIC;
    u16 *ic = (u16 *)(buf + OFF_ICON), *sm = (u16 *)(buf + OFF_SMALL), *bk = (u16 *)(buf + OFF_BACK);
    if (icon && img_png_fit(icon, ilen, ICON_W, ICON_H, ic) == 0) {
        img_scale565(ic, ICON_W, ICON_H, sm, SMALL_W, SMALL_H);
        h->flags |= 1;
    }
    if (pic && img_png_fill(pic, plen, PIC_W, PIC_H, bk) == 0) h->flags |= 2;
    g->has_icon = (h->flags & 1) != 0;
    g->has_pic = (h->flags & 2) != 0;
    char p[64];
    cache_path(g->key, p, sizeof(p));
    if (!h->flags) { sceIoRemove(p); return; }
    SceUID fd = sceIoOpen(p, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, buf, sizeof(buf));
    sceIoClose(fd);
}

static void set_titles(ShGame *g, const char *sfo_title, const char *name_src)
{
    char t[SH_TITLE_MAX];
    sh_tidy_name(name_src, g->file_title, sizeof(g->file_title));
    g->title_ascii = sfo_title && sfo_title[0] && sh_utf8_ascii(sfo_title, t, sizeof(t));
    if (g->title_ascii) {
        /* strip the spaces some games pad their titles with */
        int s = 0, e = strlen(t);
        while (t[s] == ' ') s++;
        while (e > s && t[e - 1] == ' ') e--;
        t[e] = 0;
        sh_strcpy(g->title, sizeof(g->title), t + s);
        if (!g->title[0]) g->title_ascii = 0;
    }
    if (!g->title_ascii) sh_strcpy(g->title, sizeof(g->title), g->file_title);
}

static void tidy_id(char *id)
{
    char *d = id;
    for (char *s = id; *s; s++) if (*s != '-' && *s != ' ') *d++ = *s;
    *d = 0;
}

static int read_iso(ShGame *g, const char *path)
{
    ShIso iso;
    char title[160] = "", id[16] = "";
    g->type = GT_ISO;
    if (sh_ends_with(path, ".dax") || iso_open(&iso, path) < 0) {   /* DAX is left to ARK: name from the file */
        set_titles(g, NULL, path);
        return 0;
    }
    u32 n = 0;
    u8 *sfo = iso_load(&iso, "PSP_GAME/PARAM.SFO", MAX_SFO, &n);
    if (!sfo) {
        /* not a game (a UMD video or music disc) unless the image could not be read at all */
        u8 pvd[8];
        int readable = iso_read(&iso, 16 * 2048, pvd, 8) == 0 && !memcmp(pvd + 1, "CD001", 5);
        iso_close(&iso);
        if (readable) return -1;
        set_titles(g, NULL, path);
        return 0;
    }
    sfo_get(sfo, n, "TITLE", title, sizeof(title));
    sfo_get(sfo, n, "DISC_ID", id, sizeof(id));
    free(sfo);
    tidy_id(id);
    sh_strcpy(g->id, sizeof(g->id), id);
    set_titles(g, title, path);
    u32 il = 0, pl = 0;
    u8 *icon = iso_load(&iso, "PSP_GAME/ICON0.PNG", MAX_PNG, &il);
    u8 *pic = iso_load(&iso, "PSP_GAME/PIC1.PNG", MAX_PNG, &pl);
    iso_close(&iso);
    cache_write(g, icon, il, pic, pl);
    free(icon); free(pic);
    return 0;
}

typedef struct { u32 magic, version, off[8]; } PbpHead;      /* SFO ICON0 ICON1 PIC0 PIC1 SND0 PSP PSAR */

static u8 *pbp_section(SceUID fd, const PbpHead *h, int k, u32 max, u32 *len)
{
    u32 a = h->off[k], b = h->off[k + 1];
    if (b <= a || b - a > max) return NULL;
    u8 *buf = malloc(b - a + 1);
    if (!buf) return NULL;
    if (sceIoLseek32(fd, a, PSP_SEEK_SET) != (int)a || sceIoRead(fd, buf, b - a) != (int)(b - a)) { free(buf); return NULL; }
    buf[b - a] = 0;
    *len = b - a;
    return buf;
}

/* 0 = a game to show, -1 = not one (wrong category, damaged, not a PBP). */
static int read_eboot(ShGame *g, const char *path)
{
    char title[160] = "", id[16] = "", cat[8] = "";
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    PbpHead h;
    if (sceIoRead(fd, &h, sizeof(h)) != (int)sizeof(h) || h.magic != 0x50425000u) { sceIoClose(fd); return -1; }
    u32 n = 0;
    u8 *sfo = pbp_section(fd, &h, 0, MAX_SFO, &n);
    if (sfo) {
        sfo_get(sfo, n, "TITLE", title, sizeof(title));
        sfo_get(sfo, n, "DISC_ID", id, sizeof(id));
        sfo_get(sfo, n, "CATEGORY", cat, sizeof(cat));
        free(sfo);
    }
    if (!cat[0] || !strcmp(cat, "MG")) g->type = GT_HOMEBREW;
    else if (!strcmp(cat, "EG")) g->type = GT_PSN;
    else if (!strcmp(cat, "ME")) g->type = GT_PS1;
    else { sceIoClose(fd); return -1; }        /* updates, DLC, save data */
    tidy_id(id);
    if (g->type != GT_HOMEBREW || strcmp(id, "UCJS10041")) sh_strcpy(g->id, sizeof(g->id), id);   /* the SDK's default id says nothing */
    /* the folder's name stands in for the file name */
    char folder[SH_PATH_MAX];
    sh_strcpy(folder, sizeof(folder), path);
    char *slash = strrchr(folder, '/');
    if (slash) *slash = 0;
    set_titles(g, title, folder);
    u32 il = 0, pl = 0;
    u8 *icon = pbp_section(fd, &h, 1, MAX_PNG, &il);
    u8 *pic = pbp_section(fd, &h, 4, MAX_PNG, &pl);
    sceIoClose(fd);
    cache_write(g, icon, il, pic, pl);
    free(icon); free(pic);
    return 0;
}

u8 *scan_load_pic(const ShGame *g, u32 *len)
{
    u8 *pic = NULL;
    *len = 0;
    if (g->type == GT_ISO) {
        ShIso iso;
        if (sh_ends_with(g->path, ".dax") || iso_open(&iso, g->path) < 0) return NULL;
        pic = iso_load(&iso, "PSP_GAME/PIC1.PNG", MAX_PNG, len);
        iso_close(&iso);
    } else {
        SceUID fd = sceIoOpen(g->path, PSP_O_RDONLY, 0);
        if (fd < 0) return NULL;
        PbpHead h;
        if (sceIoRead(fd, &h, sizeof(h)) == (int)sizeof(h) && h.magic == 0x50425000u) pic = pbp_section(fd, &h, 4, MAX_PNG, len);
        sceIoClose(fd);
    }
    return pic;
}

int scan_read_game(ShGame *g, const char *path, int is_eboot)
{
    sh_strcpy(g->path, sizeof(g->path), path);
    g->key = sh_crc32(0, path, strlen(path));
    return is_eboot ? read_eboot(g, path) : read_iso(g, path);
}

/* ---- the walk ------------------------------------------------------------ */
typedef struct { char path[SH_PATH_MAX]; u32 size, mtime; u8 eboot; } Found;
typedef struct { Found *f; int n, cap; const char *self; } Walk;

static u32 pack_time(const ScePspDateTime *t)
{
    return sh_minutes(t->year, t->month, t->day, t->hour, t->minute) * 60 + t->second;
}

static void add_found(Walk *w, const char *path, u32 size, u32 mtime, int eboot)
{
    if (w->n >= SH_MAX_GAMES) return;            /* the screens hold this many */
    if (w->n == w->cap) {
        int cap = w->cap ? w->cap * 2 : 128;
        Found *f = realloc(w->f, cap * sizeof(Found));
        if (!f) return;
        w->f = f; w->cap = cap;
    }
    Found *x = &w->f[w->n++];
    sh_strcpy(x->path, sizeof(x->path), path);
    x->size = size; x->mtime = mtime; x->eboot = (u8)eboot;
}

static int skip_name(const char *n)
{
    return n[0] == '.' || n[0] == '%' || !strncmp(n, "__SCE__", 7);
}

static void walk_iso(Walk *w, const char *dir, int depth)
{
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent e;
    char p[SH_PATH_MAX];
    for (;;) {
        memset(&e, 0, sizeof(e));
        if (sceIoDread(d, &e) <= 0) break;
        if (skip_name(e.d_name)) continue;
        if (sh_snprintf(p, sizeof(p), "%s/%s", dir, e.d_name) >= (int)sizeof(p)) continue;
        if (FIO_S_ISDIR(e.d_stat.st_mode)) { if (depth < 3) walk_iso(w, p, depth + 1); continue; }
        if (sh_ends_with(e.d_name, ".iso") || sh_ends_with(e.d_name, ".cso") || sh_ends_with(e.d_name, ".zso") || sh_ends_with(e.d_name, ".dax"))
            add_found(w, p, (u32)e.d_stat.st_size, pack_time(&e.d_stat.sce_st_mtime), 0);
    }
    sceIoDclose(d);
}

static void walk_game(Walk *w, const char *dir, int depth)
{
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent e;
    char p[SH_PATH_MAX], eb[SH_PATH_MAX];
    for (;;) {
        memset(&e, 0, sizeof(e));
        if (sceIoDread(d, &e) <= 0) break;
        if (!FIO_S_ISDIR(e.d_stat.st_mode) || skip_name(e.d_name)) continue;
        if (sh_snprintf(p, sizeof(p), "%s/%s", dir, e.d_name) >= (int)sizeof(p) - 11) continue;
        sh_snprintf(eb, sizeof(eb), "%s/EBOOT.PBP", p);
        SceIoStat st;
        memset(&st, 0, sizeof(st));
        if (sceIoGetstat(eb, &st) >= 0) {
            if (!(w->self && !sh_casecmp(eb, w->self)))
                add_found(w, eb, (u32)st.st_size, pack_time(&st.sce_st_mtime), 1);
        } else if (depth < 3) {
            walk_game(w, p, depth + 1);           /* a category folder: PORTS, PSX, APPS ... */
        }
    }
    sceIoDclose(d);
}

static int load_lib(ShLibrary *old)
{
    old->g = NULL; old->n = 0;
    SceUID fd = sceIoOpen(SH_LIB_PATH, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    LibHead h;
    if (sceIoRead(fd, &h, sizeof(h)) == (int)sizeof(h) && h.magic == LIB_MAGIC && h.version == LIB_VERSION &&
        h.entry == sizeof(ShGame) && h.count <= SH_MAX_GAMES) {
        old->g = malloc(h.count * sizeof(ShGame) + 1);
        if (old->g && sceIoRead(fd, old->g, h.count * sizeof(ShGame)) == (int)(h.count * sizeof(ShGame))) old->n = h.count;
    }
    sceIoClose(fd);
    return 0;
}

static void save_lib(const ShLibrary *lib)
{
    LibHead h = { LIB_MAGIC, LIB_VERSION, sizeof(ShGame), (u32)lib->n };
    SceUID fd = sceIoOpen(SH_LIB_PATH ".NEW", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return;
    int ok = sceIoWrite(fd, &h, sizeof(h)) == (int)sizeof(h) &&
             sceIoWrite(fd, lib->g, lib->n * sizeof(ShGame)) == (int)(lib->n * sizeof(ShGame));
    sceIoClose(fd);
    if (!ok) { sceIoRemove(SH_LIB_PATH ".NEW"); return; }
    sceIoRemove(SH_LIB_PATH);
    sceIoRename(SH_LIB_PATH ".NEW", SH_LIB_PATH);
}

/* ---- the small icons in one file ---------------------------------------------
 * CACHE/SMALL.BIN: "SHS1", count, the games' keys, then one 72x40 picture per
 * game in library order. Shelf reads it in one go at start instead of opening
 * every game's cache file. Rewritten whenever the library changes. */
#define PACK_MAGIC 0x31534853u
#define PACK_PATH  SH_CACHE_DIR "/SMALL.BIN"
#define SMALL_BYTES (SMALL_W * SMALL_H * 2)

u16 *scan_small_load(const ShLibrary *lib)
{
    if (lib->n <= 0) return NULL;
    SceUID fd = sceIoOpen(PACK_PATH, PSP_O_RDONLY, 0);
    if (fd < 0) return NULL;
    u32 h[2];
    u32 *keys = malloc(lib->n * 4);
    u16 *px = NULL;
    int ok = keys && sceIoRead(fd, h, 8) == 8 && h[0] == PACK_MAGIC && h[1] == (u32)lib->n &&
             sceIoRead(fd, keys, lib->n * 4) == lib->n * 4;
    for (int i = 0; ok && i < lib->n; i++) ok = keys[i] == lib->g[i].key;
    if (ok) {
        px = malloc(lib->n * SMALL_BYTES);
        if (px && sceIoRead(fd, px, lib->n * SMALL_BYTES) != lib->n * SMALL_BYTES) { free(px); px = NULL; }
    }
    free(keys);
    sceIoClose(fd);
    return px;
}

static void small_save(const ShLibrary *lib)
{
    static u16 one[SMALL_W * SMALL_H];
    SceUID fd = sceIoOpen(PACK_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return;
    u32 h[2] = { PACK_MAGIC, (u32)lib->n };
    int ok = sceIoWrite(fd, h, 8) == 8;
    for (int i = 0; ok && i < lib->n; i++) ok = sceIoWrite(fd, &lib->g[i].key, 4) == 4;
    for (int i = 0; ok && i < lib->n; i++) {
        if (!(lib->g[i].type != GT_SKIP && lib->g[i].has_icon && cache_read(lib->g[i].key, PIC_SMALL, one) == 0))
            memset(one, 0, sizeof(one));
        ok = sceIoWrite(fd, one, SMALL_BYTES) == SMALL_BYTES;
    }
    sceIoClose(fd);
    if (!ok) sceIoRemove(PACK_PATH);
}

static int small_valid(const ShLibrary *lib)
{
    u32 h[2];
    SceUID fd = sceIoOpen(PACK_PATH, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    int ok = sceIoRead(fd, h, 8) == 8 && h[0] == PACK_MAGIC && h[1] == (u32)lib->n;
    sceIoClose(fd);
    return ok;
}

/* Cache files of games that are gone: removed, so the folder does not grow for ever. */
static int cache_sweep(const ShLibrary *lib)
{
    SceUID d = sceIoDopen(SH_CACHE_DIR);
    if (d < 0) return 0;
    u32 *gone = NULL;
    int n = 0, cap = 0;
    SceIoDirent e;
    for (;;) {
        memset(&e, 0, sizeof(e));
        if (sceIoDread(d, &e) <= 0) break;
        if (strlen(e.d_name) != 12 || !sh_ends_with(e.d_name, ".IMG")) continue;
        u32 key = 0; int hex = 1;
        for (int i = 0; i < 8 && hex; i++) {
            char c = e.d_name[i];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (v < 0) hex = 0; else key = key << 4 | (u32)v;
        }
        if (!hex) continue;
        int used = 0;
        for (int i = 0; i < lib->n && !used; i++) used = lib->g[i].key == key && lib->g[i].type != GT_SKIP;
        if (used) continue;
        if (n == cap) { cap = cap ? cap * 2 : 32; u32 *t = realloc(gone, cap * 4); if (!t) break; gone = t; }
        gone[n++] = key;
    }
    sceIoDclose(d);
    for (int i = 0; i < n; i++) { char p[64]; cache_path(gone[i], p, sizeof(p)); sceIoRemove(p); }
    free(gone);
    return n;
}

int scan_library(ShLibrary *lib, const char *self, u32 now, int force, ShProgress cb)
{
    ShLibrary old;
    Walk w = { NULL, 0, 0, self };
    sh_mkdirs();
    load_lib(&old);
    walk_iso(&w, "ms0:/ISO", 0);
    walk_game(&w, "ms0:/PSP/GAME", 0);

    lib->g = calloc(w.n + 1, sizeof(ShGame));
    lib->n = 0;
    int fresh = 0, todo = 0, done = 0, changed = old.n != w.n;
    int *reuse = calloc(w.n + 1, sizeof(int));
    for (int i = 0; i < w.n; i++) {
        reuse[i] = -1;
        for (int k = 0; k < old.n; k++)
            if (old.g[k].size == w.f[i].size && old.g[k].mtime == w.f[i].mtime && !strcmp(old.g[k].path, w.f[i].path)) { reuse[i] = k; break; }
        if (reuse[i] < 0 || force) todo++;
    }
    for (int i = 0; i < w.n && lib->g; i++) {
        ShGame *g = &lib->g[lib->n];
        if (reuse[i] >= 0 && !force) { *g = old.g[reuse[i]]; lib->n++; continue; }
        if (cb) cb(done, todo, w.f[i].path);
        memset(g, 0, sizeof(*g));
        int r = scan_read_game(g, w.f[i].path, w.f[i].eboot);
        done++;
        changed = 1;
        if (r < 0) {                              /* kept, so it is not read again next time, but never shown */
            char keep[SH_PATH_MAX];
            sh_strcpy(keep, sizeof(keep), w.f[i].path);
            memset(g, 0, sizeof(*g));
            sh_strcpy(g->path, sizeof(g->path), keep);
            g->type = GT_SKIP;
        }
        g->size = w.f[i].size;
        g->mtime = w.f[i].mtime;
        g->added = now;
        for (int k = 0; k < old.n; k++)          /* a game that was only updated keeps its date */
            if (!strcmp(old.g[k].path, g->path)) { g->added = old.g[k].added; break; }
        if (reuse[i] >= 0 && old.g[reuse[i]].added) g->added = old.g[reuse[i]].added;
        lib->n++;
        fresh++;
    }
    if (cb && todo) cb(todo, todo, "");
    int swept = 0;
    if (changed || force) { save_lib(lib); swept = cache_sweep(lib); }
    if (changed || force || !small_valid(lib)) small_save(lib);
    sh_log("library: %d games (%d read anew, %d found, %d old pictures removed)", lib->n, fresh, w.n, swept);
    free(reuse); free(w.f); free(old.g);
    return fresh;
}

void scan_free(ShLibrary *lib)
{
    free(lib->g);
    lib->g = NULL; lib->n = 0;
}
