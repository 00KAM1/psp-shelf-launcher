/*
 * Shelf - ARK-5's settings, its plugin list, and Shelf as ARK's launcher.
 *
 * SETTINGS.TXT and PLUGINS.TXT lines are "runlevel, key, on|off". ARK reads
 * them top to bottom and a later line wins. Shelf changes only the lines for
 * the setting being changed whose runlevel is always/all (or game/vsh for the
 * CPU speed): it removes them and adds one new line at the end. Every other
 * line, per-game lines such as "ms0:/PSP/GAME/X/EBOOT.PBP, highmem, on"
 * included, is kept as it was. The keys and their meanings are ARK-5's
 * (SystemControl/src/settings.c).
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "common.h"
#include "arkcfg.h"

int tf_load(ShText *t, const char *path)
{
    static char buf[TF_MAX_LINES * TF_LINE];
    memset(t, 0, sizeof(*t));
    sh_strcpy(t->path, sizeof(t->path), path);
    int n = sh_read_file(path, buf, sizeof(buf));
    if (n < 0) return 0;
    for (char *l = buf; l && *l && t->n < TF_MAX_LINES;) {
        char *next = strchr(l, '\n');
        if (next) *next++ = 0;
        int len = strlen(l);
        if (len && l[len - 1] == '\r') l[--len] = 0;
        sh_strcpy(t->line[t->n++], TF_LINE, l);
        l = next;
    }
    return 0;
}

int tf_save(ShText *t)
{
    char tmp[140];
    sh_snprintf(tmp, sizeof(tmp), "%s.NEW", t->path);
    SceUID fd = sceIoOpen(tmp, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return -1;
    int ok = 1;
    for (int i = 0; i < t->n && ok; i++) {
        char l[TF_LINE + 4];
        int len = sh_snprintf(l, sizeof(l), "%s\r\n", t->line[i]);
        ok = sceIoWrite(fd, l, len) == len;
    }
    if (sceIoClose(fd) < 0) ok = 0;
    if (!ok) { sceIoRemove(tmp); return -2; }
    sceIoRemove(t->path);
    if (sceIoRename(tmp, t->path) < 0) return -3;
    t->dirty = 0;
    return 0;
}

/* Splits "a, b, c" into trimmed fields; returns how many (at most 3). */
static int fields(const char *line, char f[3][TF_LINE])
{
    int n = 0;
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '#' || *p == ';' || (p[0] == '/' && p[1] == '/')) return 0;
    while (n < 3) {
        const char *c = n < 2 ? strchr(p, ',') : NULL;
        int len = c ? (int)(c - p) : (int)strlen(p);
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
        if (len >= TF_LINE) len = TF_LINE - 1;
        memcpy(f[n], p, len); f[n][len] = 0;
        n++;
        if (!c) break;
        p = c + 1;
        while (*p == ' ' || *p == '\t') p++;
    }
    return n;
}

static int is_on(const char *v)
{
    return !sh_casecmp(v, "on") || !sh_casecmp(v, "1") || !sh_casecmp(v, "true") || !sh_casecmp(v, "enabled");
}

static int rl_is(const char *rl, const char *a, const char *b, const char *c)
{
    return (a && !sh_casecmp(rl, a)) || (b && !sh_casecmp(rl, b)) || (c && !sh_casecmp(rl, c));
}

static void remove_line(ShText *t, int i)
{
    memmove(t->line[i], t->line[i + 1], (size_t)(t->n - i - 1) * TF_LINE);
    t->n--;
}

static void add_line(ShText *t, const char *l)
{
    if (t->n >= TF_MAX_LINES) return;
    sh_strcpy(t->line[t->n++], TF_LINE, l);
}

/* ---- the settings Shelf offers ---- */
typedef struct { const char *base; const char *keys[8]; } KeyMap;   /* keys[0] = off (written "base, off") */

static const KeyMap g_maps[ARK_OPT_COUNT] = {
    [ARK_HIGHMEM]   = { "highmem", { "highmem", "highmem", "highmem:auto", "highmem:force16", "highmem:forcemax" } },
    [ARK_USBCHARGE] = { "usbcharge", { "usbcharge", "usbcharge" } },
    [ARK_MSCACHE]   = { "mscache", { "mscache", "mscache:4k", "mscache:8k", "mscache:16k" } },
    [ARK_ISOCACHE]  = { "infernocache", { "infernocache", "infernocache:lru", "infernocache:rr" } },
    [ARK_SKIPLOGOS] = { "skiplogos", { "skiplogos", "skiplogos", "skiplogos:gameboot", "skiplogos:coldboot" } },
    [ARK_HIDEPICS]  = { "hidepics", { "hidepics", "hidepics", "hidepics:pic0", "hidepics:pic1" } },
    [ARK_HIDEMAC]   = { "hidemac", { "hidemac", "hidemac" } },
    [ARK_HIDEDLC]   = { "hidedlc", { "hidedlc", "hidedlc" } },
    [ARK_NOLED]     = { "noled", { "noled", "noled" } },
    [ARK_NOUMD]     = { "noumd", { "noumd", "noumd" } },
    [ARK_NOANALOG]  = { "noanalog", { "noanalog", "noanalog" } },
    [ARK_WPA2]      = { "wpa2", { "wpa2", "wpa2" } },
    [ARK_LAUNCHER]  = { "launcher", { "launcher", "launcher" } },
};

#define CLOCKS "Auto", "133 MHz", "222 MHz", "333 MHz", "383 MHz", "403 MHz", "423 MHz", "443 MHz"
static const int g_clock_mhz[8] = { 0, 133, 222, 333, 383, 403, 423, 443 };

const ArkOptInfo g_ark_opts[ARK_OPT_COUNT] = {
    [ARK_CLOCK_GAME] = { "CPU speed in games", "Above 333 MHz is overclocking.", 8, { CLOCKS } },
    [ARK_CLOCK_XMB]  = { "CPU speed in the XMB", "Speed in the XMB and in Shelf.", 8, { CLOCKS } },
    [ARK_HIGHMEM]    = { "Extra memory", "Sidequest needs Off. Per-game lines are kept.", 5, { "Off", "On", "Auto", "Force +16 MB", "Force max" } },
    [ARK_USBCHARGE]  = { "USB charging", "Charge from a computer's USB port.", 2, { "Off", "On" } },
    [ARK_MSCACHE]    = { "Memory Stick cache", "Faster Memory Stick reads.", 4, { "Off", "4 KB", "8 KB", "16 KB" } },
    [ARK_ISOCACHE]   = { "ISO cache", "Read cache for ISO games (Inferno).", 3, { "Off", "LRU", "RR" } },
    [ARK_SKIPLOGOS]  = { "Skip Sony logos", "Skips the start-up and game-start animations.", 4, { "Off", "All", "Game start", "Power on" } },
    [ARK_HIDEPICS]   = { "Hide pictures in the XMB", "Hides games' backgrounds in the XMB.", 4, { "Off", "All", "PIC0", "PIC1" } },
    [ARK_HIDEMAC]    = { "Hide MAC address", "", 2, { "Off", "On" } },
    [ARK_HIDEDLC]    = { "Hide DLC in the XMB", "", 2, { "Off", "On" } },
    [ARK_NOLED]      = { "Turn off LEDs", "", 2, { "Off", "On" } },
    [ARK_NOUMD]      = { "Turn off the UMD drive", "Saves battery; UMD discs will not work.", 2, { "Off", "On" } },
    [ARK_NOANALOG]   = { "Turn off the analog stick", "For a stick that drifts.", 2, { "Off", "On" } },
    [ARK_WPA2]       = { "WPA2 Wi-Fi", "", 2, { "Off", "On" } },
    [ARK_LAUNCHER]   = { "Launcher mode", "Start in ARK's launcher instead of the XMB.", 2, { "Off", "On" } },
};

static int key_family(const char *key, const char *base)
{
    int n = strlen(base);
    return !strncasecmp(key, base, n) && (key[n] == 0 || key[n] == ':');
}

static int clock_value(const char *key)
{
    if (!sh_casecmp(key, "overclock")) return 333;
    if (!sh_casecmp(key, "powersave")) return 133;
    if (!sh_casecmp(key, "defaultclock")) return 222;
    if (!strncasecmp(key, "cpuclock:", 9)) return atoi(key + 9);
    return -1;
}

int ark_get(const ShText *t, int opt)
{
    char f[3][TF_LINE];
    int val = 0;
    if (opt == ARK_CLOCK_GAME || opt == ARK_CLOCK_XMB) {
        int mhz = 0;
        for (int i = 0; i < t->n; i++) {
            if (fields(t->line[i], f) != 3) continue;
            int c = clock_value(f[1]);
            if (c < 0) continue;
            if (!(rl_is(f[0], "always", "all", NULL) || (opt == ARK_CLOCK_GAME ? rl_is(f[0], "game", NULL, NULL) : rl_is(f[0], "vsh", "xmb", NULL)))) continue;
            if (is_on(f[2])) mhz = c;
            else if (mhz == c) mhz = 0;
        }
        for (int k = 0; k < 8; k++) if (g_clock_mhz[k] == mhz) return k;
        return 0;
    }
    const KeyMap *m = &g_maps[opt];
    for (int i = 0; i < t->n; i++) {
        if (fields(t->line[i], f) != 3 || !rl_is(f[0], "always", "all", NULL) || !key_family(f[1], m->base)) continue;
        if (!is_on(f[2])) { val = 0; continue; }
        val = 1;
        for (int k = 1; k < g_ark_opts[opt].nvals; k++) if (m->keys[k] && !sh_casecmp(f[1], m->keys[k])) { val = k; break; }
    }
    return val;
}

void ark_set(ShText *t, int opt, int val)
{
    char f[3][TF_LINE], l[TF_LINE];
    if (opt == ARK_CLOCK_GAME || opt == ARK_CLOCK_XMB) {
        int other = ark_get(t, opt == ARK_CLOCK_GAME ? ARK_CLOCK_XMB : ARK_CLOCK_GAME);
        int game = opt == ARK_CLOCK_GAME ? val : other, xmb = opt == ARK_CLOCK_XMB ? val : other;
        for (int i = t->n - 1; i >= 0; i--)
            if (fields(t->line[i], f) == 3 && clock_value(f[1]) >= 0 && rl_is(f[0], "always", "all", "game")) remove_line(t, i);
            else if (fields(t->line[i], f) == 3 && clock_value(f[1]) >= 0 && rl_is(f[0], "vsh", "xmb", NULL)) remove_line(t, i);
        if (game > 0) { sh_snprintf(l, sizeof(l), "game, cpuclock:%d, on", g_clock_mhz[game]); add_line(t, l); }
        if (xmb > 0) { sh_snprintf(l, sizeof(l), "vsh, cpuclock:%d, on", g_clock_mhz[xmb]); add_line(t, l); }
        t->dirty = 1;
        return;
    }
    const KeyMap *m = &g_maps[opt];
    for (int i = t->n - 1; i >= 0; i--)
        if (fields(t->line[i], f) == 3 && rl_is(f[0], "always", "all", NULL) && key_family(f[1], m->base)) remove_line(t, i);
    if (val <= 0) sh_snprintf(l, sizeof(l), "always, %s, off", m->base);
    else sh_snprintf(l, sizeof(l), "always, %s, on", m->keys[val]);
    add_line(t, l);
    t->dirty = 1;
}

/* ---- PLUGINS.TXT ---- */
int plugins_list(const ShText *t, ShPlugin *out, int max)
{
    char f[3][TF_LINE];
    int n = 0;
    for (int i = 0; i < t->n && n < max; i++) {
        if (fields(t->line[i], f) != 3) continue;
        ShPlugin *p = &out[n];
        p->line = i;
        const char *name = strrchr(f[1], '/');
        sh_strcpy(p->name, sizeof(p->name), name ? name + 1 : f[1]);
        const char *rl = f[0];
        sh_strcpy(p->where, sizeof(p->where), rl);
        if (strchr(rl, '/')) {                    /* a per-game line: name the game's folder */
            char dir[TF_LINE];
            sh_strcpy(dir, sizeof(dir), rl);
            char *e = strrchr(dir, '/');
            if (e && sh_ends_with(dir, ".PBP")) *e = 0;
            e = strrchr(dir, '/');
            char lab[48];
            sh_snprintf(lab, sizeof(lab), "%s (%s)", p->name, e ? e + 1 : dir);
            sh_strcpy(p->name, sizeof(p->name), lab);
            sh_strcpy(p->where, sizeof(p->where), e ? e + 1 : dir);
        } else if (strchr(rl, '.') == NULL && strlen(rl) == 9) {}   /* a game ID: shown as is */
        p->on = is_on(f[2]);
        n++;
    }
    return n;
}

void plugins_set(ShText *t, const ShPlugin *p, int on)
{
    char *l = t->line[p->line];
    char *last = strrchr(l, ',');
    if (!last) return;
    char head[TF_LINE];
    int keep = (int)(last - l);
    memcpy(head, l, keep); head[keep] = 0;
    sh_snprintf(l, TF_LINE, "%s, %s", head, on ? "on" : "off");
    t->dirty = 1;
}

/* ---- Shelf as the launcher ---- */
static int exists(const char *p) { SceIoStat st; memset(&st, 0, sizeof(st)); return sceIoGetstat(p, &st) >= 0; }

static int pbp_is_shelf(const char *path)
{
    u32 h[10];
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    int ok = sceIoRead(fd, h, sizeof(h)) == (int)sizeof(h) && h[0] == 0x50425000u && h[3] > h[2] && h[3] - h[2] < 4096;
    char sfo[4096];
    int n = ok ? (int)(h[3] - h[2]) : 0;
    ok = ok && sceIoLseek32(fd, h[2], PSP_SEEK_SET) == (int)h[2] && sceIoRead(fd, sfo, n) == n;
    sceIoClose(fd);
    if (!ok) return 0;
    for (int i = 0; i + 6 <= n; i++) if (!memcmp(sfo + i, "SHELF\0", 6) || !memcmp(sfo + i, "Shelf\0", 6)) return 1;
    return 0;
}

int launcher_installed(const char *ark_dir)
{
    char p[160];
    sh_snprintf(p, sizeof(p), "%sVBOOT.PBP", ark_dir);
    return pbp_is_shelf(p);
}

static int copy_file(const char *from, const char *to)
{
    static char buf[64 * 1024];
    SceUID in = sceIoOpen(from, PSP_O_RDONLY, 0);
    if (in < 0) return -1;
    SceUID out = sceIoOpen(to, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out < 0) { sceIoClose(in); return -2; }
    int n, r = 0;
    while ((n = sceIoRead(in, buf, sizeof(buf))) > 0) if (sceIoWrite(out, buf, n) != n) { r = -3; break; }
    if (n < 0) r = -4;
    sceIoClose(in);
    if (sceIoClose(out) < 0) r = -5;
    if (r) sceIoRemove(to);
    return r;
}

#define BEFORE_MARK SH_DIR "/launcher_before.txt"
#define PAUSE_MARK SH_DIR "/launcher.paused"

#define SOURCE_MARK SH_DIR "/launcher_source.txt"

/* The size and date of the EBOOT the start-up copy was made from. */
static void source_text(const char *self, char *out, int size)
{
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    out[0] = 0;
    if (sceIoGetstat(self, &st) < 0) return;
    const ScePspDateTime *t = &st.sce_st_mtime;
    sh_snprintf(out, size, "%u %04d-%02d-%02d %02d:%02d:%02d", (unsigned)st.st_size, t->year, t->month, t->day, t->hour, t->minute, t->second);
}

static void source_remember(const char *self)
{
    char b[64];
    source_text(self, b, sizeof(b));
    SceUID fd = sceIoOpen(SOURCE_MARK, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) { sceIoWrite(fd, b, strlen(b)); sceIoClose(fd); }
}

/* A newer Shelf was copied to the Memory Stick: bring the start-up copy
 * (VBOOT.PBP) up to date. 1 = it was replaced. */
int launcher_update(const char *ark_dir, const char *self)
{
    char vboot[160], tmp[160], now[64], was[64] = "";
    sh_snprintf(vboot, sizeof(vboot), "%sVBOOT.PBP", ark_dir);
    sh_snprintf(tmp, sizeof(tmp), "%sVBOOT.NEW", ark_dir);
    if (!sh_casecmp(self, vboot) || !launcher_installed(ark_dir)) return 0;
    source_text(self, now, sizeof(now));
    sh_read_file(SOURCE_MARK, was, sizeof(was));
    if (!now[0] || !strcmp(now, was)) return 0;
    if (!pbp_is_shelf(self) || copy_file(self, tmp) < 0) return 0;
    sceIoRemove(vboot);
    if (sceIoRename(tmp, vboot) < 0) { copy_file(self, vboot); sceIoRemove(tmp); }
    source_remember(self);
    return 1;
}

/* Shelf is the launcher: it goes in as ARK's VBOOT.PBP, whatever was there. The file
 * it replaces is set aside once as VBOOT_BEFORE_SHELF.PBP and never used again by
 * Shelf. Returns 0 when VBOOT.PBP is Shelf. */
static int put_in_place(const char *ark_dir, const char *self)
{
    char vboot[160], keep[160], tmp[160];
    sh_snprintf(vboot, sizeof(vboot), "%sVBOOT.PBP", ark_dir);
    sh_snprintf(keep, sizeof(keep), "%sVBOOT_BEFORE_SHELF.PBP", ark_dir);
    sh_snprintf(tmp, sizeof(tmp), "%sVBOOT.NEW", ark_dir);
    if (launcher_installed(ark_dir)) return 0;
    if (!exists(self) || !pbp_is_shelf(self)) return -1;
    if (copy_file(self, tmp) < 0) return -3;
    if (exists(vboot) && !exists(keep)) sceIoRename(vboot, keep);
    sceIoRemove(vboot);
    if (sceIoRename(tmp, vboot) < 0 && copy_file(tmp, vboot) < 0) { sceIoRemove(tmp); return -5; }
    sceIoRemove(tmp);
    source_remember(self);
    return 0;
}

static int launcher_line(const char *ark_dir, int set);

/* Shelf opened from the Game menu hands over to its launcher copy, where the plugins
 * that treat an EBOOT.PBP as a game (Checkpoint, Sidequest, ...) stay out.
 * 1 = VBOOT.PBP is Shelf (put there now if it was not) and `vboot` holds its path. */
int launcher_ready(const char *ark_dir, const char *self, char *vboot, int size)
{
    sh_snprintf(vboot, size, "%sVBOOT.PBP", ark_dir);
    if (!sh_casecmp(self, vboot)) return 0;                   /* this is the launcher copy */
    return put_in_place(ark_dir, self) == 0;
}

int launcher_install(const char *ark_dir, const char *self, char *msg, int msglen)
{
    int r = put_in_place(ark_dir, self);
    if (r < 0) {
        sh_snprintf(msg, msglen, r == -1 ? "Shelf's own EBOOT.PBP was not found" : r == -3 ? "Could not copy Shelf (Memory Stick full?)" : "Could not put Shelf in place");
        return r;
    }
    r = launcher_line(ark_dir, 1) == 1 ? 0 : -6;
    sh_snprintf(msg, msglen, r == 0 ? "Shelf starts at power on from now" : "Shelf copied; SETTINGS.TXT could not be written");
    return r;
}

/* Start at power on, off: ARK's launcher setting goes off. Shelf stays in place as
 * VBOOT.PBP, where it is still used to come back from games. */
int launcher_remove(const char *ark_dir, char *msg, int msglen)
{
    sceIoRemove(BEFORE_MARK);
    sceIoRemove(PAUSE_MARK);
    sceIoRemove(SH_DIR "/return.once");
    int r = launcher_line(ark_dir, 0) == 0 ? 0 : -1;
    sh_snprintf(msg, msglen, r == 0 ? "The PSP starts in the XMB again" : "SETTINGS.TXT could not be written");
    return r;
}

/* ARK's launcher setting is on but its launcher is not Shelf: Shelf takes its place.
 * 1 = it did. */
int launcher_takeover(const char *ark_dir, const char *self)
{
    if (launcher_installed(ark_dir) || launcher_line(ark_dir, -1) != 1) return 0;
    return put_in_place(ark_dir, self) == 0;
}

/* ---- back to Shelf after a game, when Shelf was opened from the XMB ---------------
 * ARK only knows "launcher on" (then every game exit and every power-on goes to
 * VBOOT.PBP) or "launcher off" (the XMB), and it reads the setting from the file at
 * every restart. So: just before a game is started the launcher is switched on, with
 * Shelf as VBOOT.PBP, and a marker is left. Leaving the game then lands in Shelf,
 * which sees the marker and switches the launcher off again at once. The PSP still
 * powers on into the XMB.
 * arm:  1 = switched on for this one game, 0 = nothing to do (already on), <0 error.
 * done: 1 = it was switched off again. */
#define RETURN_MARK SH_DIR "/return.once"

static int launcher_line(const char *ark_dir, int set)       /* set < 0: only read */
{
    char s[160];
    sh_snprintf(s, sizeof(s), "%sSETTINGS.TXT", ark_dir);
    ShText *t = malloc(sizeof(ShText));
    if (!t) return -1;
    tf_load(t, s);
    int on = ark_get(t, ARK_LAUNCHER);
    if (set >= 0 && on != set) { ark_set(t, ARK_LAUNCHER, set); if (tf_save(t) == 0) on = set; }
    free(t);
    return on;
}

int launcher_return_arm(const char *ark_dir, const char *self)
{
    char m[64];
    if (launcher_line(ark_dir, -1) != 0) { put_in_place(ark_dir, self); return 0; }   /* on already: ARK goes to its launcher, which is Shelf */
    if (launcher_install(ark_dir, self, m, sizeof(m)) != 0) return -1;
    SceUID fd = sceIoOpen(RETURN_MARK, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) { launcher_line(ark_dir, 0); return -2; }    /* no marker, no switch: it could not be undone */
    sceIoClose(fd);
    return 1;
}

int launcher_return_done(const char *ark_dir)
{
    if (!exists(RETURN_MARK)) return 0;
    if (launcher_line(ark_dir, 0) != 0) return 0;            /* could not be written: try again next start */
    sceIoRemove(RETURN_MARK);
    return 1;
}

void launcher_pause(const char *ark_dir)
{
    char s[160];
    sh_snprintf(s, sizeof(s), "%sSETTINGS.TXT", ark_dir);
    ShText *t = malloc(sizeof(ShText));
    if (!t) return;
    tf_load(t, s);
    if (ark_get(t, ARK_LAUNCHER)) {
        ark_set(t, ARK_LAUNCHER, 0);
        if (tf_save(t) == 0) { SceUID fd = sceIoOpen(PAUSE_MARK, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777); if (fd >= 0) sceIoClose(fd); }
    }
    free(t);
}

int launcher_resume_if_paused(const char *ark_dir)
{
    char s[160];
    if (!exists(PAUSE_MARK)) return 0;
    if (!launcher_installed(ark_dir)) { sceIoRemove(PAUSE_MARK); return 0; }   /* Shelf is no longer the launcher */
    sh_snprintf(s, sizeof(s), "%sSETTINGS.TXT", ark_dir);
    ShText *t = malloc(sizeof(ShText));
    if (!t) return 0;
    tf_load(t, s);
    ark_set(t, ARK_LAUNCHER, 1);
    int r = tf_save(t);
    free(t);
    if (r == 0) sceIoRemove(PAUSE_MARK);
    return r == 0;
}
