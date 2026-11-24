/*
 * Shelf - what the plugins know about each game.
 *
 *   Sidequest   ms0:/SIDEQUEST/<folder>/SUSPEND.SQS, STATE1..5.SQS (+ .THM pictures)
 *               matched to a game by the path in the slot header (boot_path)
 *   Checkpoint  ms0:/CHECKPOINT/JOURNEY/<id>.TXT  "2026-11-14 19:22 | 0h 25m | Town of Redmont | SAVE"
 *               ms0:/CHECKPOINT/JOURNEY/<id>.TIM  seconds played
 *   Emblem      ms0:/EMBLEM/games/<id>.txt  (lines "n | title | points | rule", 'none' = not counted)
 *               ms0:/EMBLEM/earned/<id>.txt ("n | 2026-11-22 14:03 | title | 10 pts")
 *   Shelf       ms0:/SHELF/games.txt  "path | launched | finished | hidden | picture"
 * Only read here; Shelf never writes the plugins' files.
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "common.h"
#include "data.h"
#include "gfx.h"

/* ---- Sidequest ----------------------------------------------------------- */
#define SQ_MAGIC 0x31535153u
#define SQ_OFF_SAVED 244
#define SQ_OFF_BOOT 276
#define SQ_HEAD_READ 404

typedef struct { char dir[48]; char boot[128]; u8 has[6]; u32 when[6]; } SqGame;

static int sq_read_dirs(SqGame **out)
{
    int n = 0, cap = 0;
    SqGame *list = NULL;
    SceUID d = sceIoDopen(SQ_DIR);
    if (d < 0) { *out = NULL; return 0; }
    SceIoDirent e;
    static u8 head[SQ_HEAD_READ];
    char p[160];
    for (;;) {
        memset(&e, 0, sizeof(e));
        if (sceIoDread(d, &e) <= 0) break;
        if (!FIO_S_ISDIR(e.d_stat.st_mode) || e.d_name[0] == '.' || strlen(e.d_name) >= 48) continue;
        SqGame g;
        memset(&g, 0, sizeof(g));
        sh_strcpy(g.dir, sizeof(g.dir), e.d_name);
        for (int k = 0; k < 6; k++) {
            if (k == 0) sh_snprintf(p, sizeof(p), SQ_DIR "/%s/SUSPEND.SQS", e.d_name);
            else sh_snprintf(p, sizeof(p), SQ_DIR "/%s/STATE%d.SQS", e.d_name, k);
            SceUID fd = sceIoOpen(p, PSP_O_RDONLY, 0);
            if (fd < 0) continue;
            int r = sceIoRead(fd, head, SQ_HEAD_READ);
            sceIoClose(fd);
            u32 magic;
            memcpy(&magic, head, 4);
            if (r != SQ_HEAD_READ || magic != SQ_MAGIC) continue;
            head[SQ_OFF_SAVED + 31] = 0;
            head[SQ_OFF_BOOT + 127] = 0;
            g.has[k] = 1;
            g.when[k] = sh_parse_stamp((const char *)head + SQ_OFF_SAVED);
            if (!g.boot[0]) sh_strcpy(g.boot, sizeof(g.boot), (const char *)head + SQ_OFF_BOOT);
        }
        if (!g.boot[0]) continue;
        if (n == cap) { cap = cap ? cap * 2 : 16; SqGame *l = realloc(list, cap * sizeof(SqGame)); if (!l) break; list = l; }
        list[n++] = g;
    }
    sceIoDclose(d);
    *out = list;
    return n;
}

void data_thm_path(const ShInfo *in, int k, char *out, int size)
{
    if (k == 0) sh_snprintf(out, size, SQ_DIR "/%s/SUSPEND.THM", in->sq_dir);
    else sh_snprintf(out, size, SQ_DIR "/%s/STATE%d.THM", in->sq_dir, k);
}

/* ---- Sidequest: "load the newest slot of this game without asking" ---------------
 * One line, the game's path, in ms0:/SIDEQUEST/RESUME.REQ. Sidequest reads it once
 * when the game starts and removes it. path NULL: take a waiting request away. */
void sq_resume_request(const char *path)
{
    if (!path) { sceIoRemove(SQ_DIR "/RESUME.REQ"); sceIoRemove(SQ_DIR "/START.REQ"); return; }
    SceUID fd = sceIoOpen(SQ_DIR "/RESUME.REQ", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, path, strlen(path));
    sceIoWrite(fd, "\r\n", 2);
    sceIoClose(fd);
}

/* "Start from...": a chosen slot, or none at all. Two lines in ms0:/SIDEQUEST/START.REQ: the
 * game's path, then the slot number 0..5 or the word none. It is a file of its own, and
 * not a second line in RESUME.REQ, because a Sidequest from before this would read only
 * the path there and load the newest slot, which is not what was chosen; a file it does
 * not know it leaves alone and asks its usual question. */
void sq_start_request(const char *path, int slot)
{
    char line[16];
    sceIoRemove(SQ_DIR "/RESUME.REQ");
    SceUID fd = sceIoOpen(SQ_DIR "/START.REQ", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return;
    if (slot >= 0 && slot <= 5) sh_snprintf(line, sizeof(line), "%d\r\n", slot); else sh_strcpy(line, sizeof(line), "none\r\n");
    sceIoWrite(fd, path, strlen(path));
    sceIoWrite(fd, "\r\n", 2);
    sceIoWrite(fd, line, strlen(line));
    sceIoClose(fd);
}

/* ---- uptime and battery -------------------------------------------------------------
 * Nothing on a PSP says "this is the same power-on as before" to a program that is
 * started afresh after every game, so Shelf keeps it in ms0:/SHELF/uptime.txt:
 *   start  start%  last-seen  last-seen%  coming-back
 * "coming-back" is 1 when Shelf itself handed over (it started a game, restarted, or
 * left for the XMB) and expects to be started again in the same power-on. With it at 0,
 * or after 12 hours without Shelf, the next start counts as a new power-on, and the one
 * before is written to ms0:/SHELF/battery.log.
 * What this cannot see: the PSP switched off while a game or the XMB was running. That
 * power-on is then carried on with, until Shelf next sees one end. */
#define UP_PATH  SH_DIR "/uptime.txt"
#define UP_LOG   SH_DIR "/battery.log"

void uptime_line(u32 from, int from_pct, u32 to, int to_pct, char *out, int size)
{
    char a[24], b[24];
    u32 mins = to > from ? to - from : 0;
    sh_format_date(from, a, sizeof(a), 1);
    sh_format_date(to, b, sizeof(b), 1);
    if (from_pct >= 0 && to_pct >= 0)
        sh_snprintf(out, size, "%s to %s | %dh %02dm | battery %d%% to %d%% | %s%d%%", a, b, (int)(mins / 60), (int)(mins % 60),
                    from_pct, to_pct, to_pct > from_pct ? "+" : "-", to_pct > from_pct ? to_pct - from_pct : from_pct - to_pct);
    else sh_snprintf(out, size, "%s to %s | %dh %02dm | no battery reading", a, b, (int)(mins / 60), (int)(mins % 60));
}

static void uptime_write(const ShUptime *u, int coming_back)
{
    char b[96];
    int n = sh_snprintf(b, sizeof(b), "%u %d %u %d %d\r\n", (unsigned)u->start, u->start_pct, (unsigned)u->seen, u->seen_pct, coming_back);
    SceUID fd = sceIoOpen(UP_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) { sceIoWrite(fd, b, n); sceIoClose(fd); }
}

/* The battery plugin (Runtime) keeps the same record in every program, with 2 as the fifth
 * field and the minutes asleep as the sixth. It keeps it in a folder of its own,
 * ms0:/RUNTIME, and not in Shelf's: Runtime runs inside games, so it is inside every
 * Sidequest state, and a write of its after a load is the one write near Shelf's files that
 * could be made with an old view of the Memory Stick. Shelf only ever reads that folder.
 *   - no file there: no Runtime. Shelf keeps its own count (above).
 *   - a good record, and fresh: Shelf shows Runtime's.
 *   - a good record gone stale: Runtime was taken out. Shelf keeps its own count.
 *   - a file that is empty or does not read as a record: u->bad, and Shelf shows no
 *     time or battery at all rather than figures it cannot stand behind. */
#define RT_PATH   "ms0:/RUNTIME/uptime.txt"
#define UP_PLUGIN 2
#define UP_FRESH  5
int uptime_refresh(ShUptime *u, u32 now)
{
    char b[96] = "";
    SceIoStat st;
    u->plugin = 0;
    u->bad = 0;
    memset(&st, 0, sizeof(st));
    if (sceIoGetstat(RT_PATH, &st) < 0) return 0;
    if (sh_read_file(RT_PATH, b, sizeof(b)) <= 0) { u->bad = 1; return 0; }
    char *p = b, *q;
    unsigned start = (unsigned)strtoul(p, &q, 10); int f1 = q != p; p = q;
    int sp = (int)strtol(p, &q, 10);               int f2 = q != p; p = q;
    unsigned seen = (unsigned)strtoul(p, &q, 10);  int f3 = q != p; p = q;
    int np = (int)strtol(p, &q, 10);               int f4 = q != p; p = q;
    int back = (int)strtol(p, &q, 10);             int f5 = q != p; p = q;
    unsigned asleep = (unsigned)strtoul(p, &p, 10);
    if (!(f1 && f2 && f3 && f4 && f5) || back != UP_PLUGIN || !start || seen < start || sp < -1 || sp > 100) { u->bad = 1; return 0; }
    if (now + UP_FRESH < seen || (now > seen && now - seen > UP_FRESH)) return 0;
    if (asleep > seen - start) asleep = seen - start;
    u->start = start; u->start_pct = sp >= 0 && sp <= 100 ? sp : -1;
    u->seen = seen; u->seen_pct = np; u->asleep = asleep; u->plugin = 1;
    return 1;
}

void uptime_begin(ShUptime *u, u32 now, int pct)
{
    u->asleep = 0;
    if (uptime_refresh(u, now)) return;
    int bad = u->bad;                                         /* Shelf's own count goes on underneath; it is only not shown */
    char b[96] = "";
    unsigned start = 0, seen = 0;
    int sp = -1, np = -1, back = 0, have = 0;
    if (sh_read_file(UP_PATH, b, sizeof(b)) > 0) {
        char *p = b;
        start = (unsigned)strtoul(p, &p, 10); sp = (int)strtol(p, &p, 10);
        seen = (unsigned)strtoul(p, &p, 10); np = (int)strtol(p, &p, 10); back = (int)strtol(p, &p, 10);
        have = start > 0 && seen >= start;
    }
    if (have && back == 1 && now >= seen && now - seen <= 12 * 60) {
        u->start = start; u->start_pct = sp;                 /* the same power-on: carry on */
    } else {
        if (have && seen > start) {                           /* the one before is over: one line for it */
            char line[160];
            uptime_line(start, sp, seen, np, line, sizeof(line));
            SceUID fd = sceIoOpen(UP_LOG, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
            if (fd >= 0) { sceIoWrite(fd, line, strlen(line)); sceIoWrite(fd, "\r\n", 2); sceIoClose(fd); }
        }
        u->start = now; u->start_pct = pct;
    }
    u->seen = now; u->seen_pct = pct;
    u->bad = bad;
    uptime_write(u, 0);
}

void uptime_mark(ShUptime *u, u32 now, int pct, int coming_back)
{
    if (u->plugin) return;                                    /* Runtime's file: not ours to write */
    u->seen = now; u->seen_pct = pct;
    uptime_write(u, coming_back);
}

/* ---- which files exist: one folder listing instead of a failed open per game ---- */
typedef struct { char (*name)[28]; int n; } Names;

static void names_load(Names *s, const char *dir)
{
    s->name = NULL; s->n = 0;
    int cap = 0;
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent e;
    for (;;) {
        memset(&e, 0, sizeof(e));
        if (sceIoDread(d, &e) <= 0) break;
        if (FIO_S_ISDIR(e.d_stat.st_mode) || strlen(e.d_name) >= 28) continue;
        if (s->n == cap) { cap = cap ? cap * 2 : 64; void *t = realloc(s->name, cap * 28); if (!t) break; s->name = t; }
        sh_strcpy(s->name[s->n++], 28, e.d_name);
    }
    sceIoDclose(d);
}

static int names_has(const Names *s, const char *id, const char *ext)
{
    char f[28];
    if (sh_snprintf(f, sizeof(f), "%s%s", id, ext) >= (int)sizeof(f)) return 0;
    for (int i = 0; i < s->n; i++) if (!sh_casecmp(s->name[i], f)) return 1;
    return 0;
}

/* ---- Checkpoint ------------------------------------------------------------ */
/* One journey line: date | time played | text | save. 0 = good. */
static int parse_entry(char *line, ShEntry *e)
{
    char *f[4];
    int n = 0;
    f[n++] = line;
    for (char *c = line; *c && n < 4; c++) if (*c == '|') { *c = 0; f[n++] = c + 1; }
    if (n < 3) return -1;
    for (int i = 0; i < n; i++) { while (*f[i] == ' ') f[i]++; char *z = f[i] + strlen(f[i]); while (z > f[i] && (z[-1] == ' ' || z[-1] == '\r')) *--z = 0; }
    e->when = sh_parse_stamp(f[0]);
    if (!e->when) return -1;
    int h = 0, m = 0;
    sscanf(f[1], "%dh %dm", &h, &m);
    e->secs = (u32)(h * 3600 + m * 60);
    u8_copy(e->text, sizeof(e->text), f[2]);
    return e->text[0] ? 0 : -1;
}

int data_journey(const char *id, ShEntry *out, int max, int *total)
{
    char p[96];
    *total = 0;
    if (!id[0]) return 0;
    sh_snprintf(p, sizeof(p), CP_JOURNEY_DIR "/%s.TXT", id);
    SceUID fd = sceIoOpen(p, PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    static char buf[2048], line[512];
    int ll = 0, n = 0, got = 0;
    ShEntry e;
    while ((n = sceIoRead(fd, buf, sizeof(buf))) > 0 || ll) {
        for (int i = 0; i <= n; i++) {
            int end = i == n ? (n == 0) : buf[i] == '\n';
            if (i == n && !end) break;
            if (end) {
                line[ll] = 0;
                if (ll && line[0] != '#' && parse_entry(line, &e) == 0) {
                    (*total)++;
                    /* keep the newest `max`, newest first */
                    if (got < max) got++;
                    memmove(out + 1, out, (got - 1) * sizeof(ShEntry));
                    out[0] = e;
                }
                ll = 0;
                if (i == n) break;
            } else if (ll < (int)sizeof(line) - 1) line[ll++] = buf[i];
        }
        if (n <= 0) break;
    }
    sceIoClose(fd);
    return got;
}

static u32 read_seconds(const char *id, const Names *have)
{
    char p[96], b[24];
    b[0] = 0;
    sh_snprintf(p, sizeof(p), CP_JOURNEY_DIR "/%s.TIM", id);
    if (!names_has(have, id, ".TIM") || sh_read_file(p, b, sizeof(b)) <= 0) {
        sh_snprintf(p, sizeof(p), CP_JOURNEY_DIR "/%s.NEW", id);
        if (!names_has(have, id, ".NEW") || sh_read_file(p, b, sizeof(b)) <= 0) return 0;
    }
    return (u32)strtoul(b, NULL, 10);
}

/* ---- Emblem ----------------------------------------------------------------- */
static char g_text[48 * 1024];

static void emblem_read(const char *id, ShInfo *in, int has_earned)
{
    char p[96];
    u8 inc[256];
    memset(inc, 0, sizeof(inc));
    in->em_total = -1; in->em_got = 0; in->em_nrecent = 0;
    if (!id[0]) return;
    sh_snprintf(p, sizeof(p), EM_DIR "/games/%s.txt", id);
    int n = sh_read_file(p, g_text, sizeof(g_text));
    if (n < 0) return;
    in->em_total = 0;
    for (char *l = g_text; l && *l;) {
        char *next = strchr(l, '\n');
        if (next) *next++ = 0;
        char *f[4]; int k = 0;
        f[k++] = l;
        for (char *c = l; *c && k < 4; c++) if (*c == '|') { *c = 0; f[k++] = c + 1; }
        if (k == 4) {
            int num = atoi(f[0]);
            char *rule = f[3];
            while (*rule == ' ' || *rule == '\t') rule++;
            int none = !strncmp(rule, "none", 4) && (rule[4] == 0 || rule[4] == ' ' || rule[4] == '\r' || rule[4] == '#');
            char *hash = l; while (*hash == ' ') hash++;
            if (num >= 1 && num <= 255 && *hash != '#' && !none && *rule && *rule != '\r' && !inc[num]) { inc[num] = 1; in->em_total++; }
        }
        l = next;
    }
    sh_snprintf(p, sizeof(p), EM_DIR "/earned/%s.txt", id);
    if (!has_earned || sh_read_file(p, g_text, sizeof(g_text)) < 0) return;
    u8 seen[256];
    memset(seen, 0, sizeof(seen));
    for (char *l = g_text; l && *l;) {
        char *next = strchr(l, '\n');
        if (next) *next++ = 0;
        char *f[4]; int k = 0;
        f[k++] = l;
        for (char *c = l; *c && k < 4; c++) if (*c == '|') { *c = 0; f[k++] = c + 1; }
        int num = atoi(f[0]);
        if (k >= 2 && num >= 1 && num <= 255 && inc[num] && !seen[num]) {
            seen[num] = 1;
            in->em_got++;
            char *d = f[1]; while (*d == ' ') d++;
            u32 when = sh_parse_stamp(d);
            char title[48] = "";
            if (k >= 3) { char *t = f[2]; while (*t == ' ') t++; u8_copy(title, sizeof(title), t); int z = strlen(title); while (z && title[z - 1] == ' ') title[--z] = 0; }
            /* keep the newest few, newest first */
            int pos = in->em_nrecent;
            while (pos > 0 && in->em_recent_time[pos - 1] < when) pos--;
            if (pos < SH_MAX_RECENT) {
                int last = in->em_nrecent < SH_MAX_RECENT ? in->em_nrecent : SH_MAX_RECENT - 1;
                for (int j = last; j > pos; j--) { in->em_recent_time[j] = in->em_recent_time[j - 1]; memcpy(in->em_recent[j], in->em_recent[j - 1], 48); }
                in->em_recent_time[pos] = when;
                sh_strcpy(in->em_recent[pos], 48, title[0] ? title : "Achievement");
                if (in->em_nrecent < SH_MAX_RECENT) in->em_nrecent++;
            }
        }
        l = next;
    }
}

/* ---- Shelf's own notes -------------------------------------------------------- */
typedef struct { char path[SH_PATH_MAX]; u32 launched, finished; int hidden, pic, fav; } Note;

static int notes_load(Note **out)
{
    static char buf[64 * 1024];
    *out = NULL;
    int len = sh_read_file(SH_STATE_PATH, buf, sizeof(buf));
    if (len <= 0) return 0;
    int n = 0, cap = 0;
    Note *list = NULL;
    for (char *l = buf; l && *l;) {
        char *next = strchr(l, '\n');
        if (next) *next++ = 0;
        if (*l != '#') {
            char *f[6]; int k = 0;
            f[k++] = l;
            for (char *c = l; *c && k < 6; c++) if (*c == '|') { *c = 0; f[k++] = c + 1; }
            if (k >= 5) {
                Note x;
                memset(&x, 0, sizeof(x));
                char *pth = f[0]; while (*pth == ' ') pth++;
                char *z = pth + strlen(pth); while (z > pth && z[-1] == ' ') *--z = 0;
                sh_strcpy(x.path, sizeof(x.path), pth);
                x.launched = strtoul(f[1], NULL, 10);
                x.finished = strtoul(f[2], NULL, 10);
                x.hidden = atoi(f[3]);
                x.pic = atoi(f[4]);
                x.fav = k >= 6 && atoi(f[5]) != 0;
                if (x.path[0]) {
                    if (n == cap) { cap = cap ? cap * 2 : 32; Note *t = realloc(list, cap * sizeof(Note)); if (!t) break; list = t; }
                    list[n++] = x;
                }
            }
        }
        l = next;
    }
    *out = list;
    return n;
}

void state_save(const ShLibrary *lib, const ShInfo *info)
{
    /* games no longer on the stick keep their line, so moving an ISO back finds it again */
    Note *old = NULL;
    int nold = notes_load(&old);
    SceUID fd = sceIoOpen(SH_STATE_PATH ".NEW", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) { free(old); return; }
    char line[SH_PATH_MAX + 64];
    int len = sh_snprintf(line, sizeof(line), "# Shelf: path | last started | finished | hidden | picture | favourite (times in minutes since 2000)\r\n");
    int ok = sceIoWrite(fd, line, len) == len;
    for (int i = 0; i < lib->n && ok; i++) {
        const ShInfo *in = &info[i];
        if (lib->g[i].type == GT_SKIP || (!in->launched && !in->finished && !in->hidden && !in->fav && in->pic < 0)) continue;
        len = sh_snprintf(line, sizeof(line), "%s | %u | %u | %d | %d | %d\r\n", lib->g[i].path, (unsigned)in->launched,
                          (unsigned)in->finished, in->hidden, in->pic, in->fav);
        ok = sceIoWrite(fd, line, len) == len;
    }
    for (int k = 0; k < nold && ok; k++) {
        int present = 0;
        for (int i = 0; i < lib->n && !present; i++) present = !strcmp(lib->g[i].path, old[k].path);
        if (present) continue;
        len = sh_snprintf(line, sizeof(line), "%s | %u | %u | %d | %d | %d\r\n", old[k].path, (unsigned)old[k].launched,
                          (unsigned)old[k].finished, old[k].hidden, old[k].pic, old[k].fav);
        ok = sceIoWrite(fd, line, len) == len;
    }
    sceIoClose(fd);
    free(old);
    if (!ok) { sceIoRemove(SH_STATE_PATH ".NEW"); return; }
    sceIoRemove(SH_STATE_PATH);
    sceIoRename(SH_STATE_PATH ".NEW", SH_STATE_PATH);
}

/* ---- all of it ---------------------------------------------------------------- */
void data_finish(const ShGame *g, ShInfo *in)
{
    u32 last = in->launched;
    if (in->sq_newest >= 0 && in->sq_time[(int)in->sq_newest] > last) last = in->sq_time[(int)in->sq_newest];
    if (in->cp_last_time > last) last = in->cp_last_time;
    in->last_played = last;
    int touched = in->launched || in->sq_newest >= 0 || in->cp_secs >= 60 || in->cp_entries > 0;
    in->playing = g->type != GT_SKIP && touched && !in->finished;
}

void data_load_all(const ShLibrary *lib, ShInfo *info, u32 now)
{
    SqGame *sq = NULL;
    int nsq = sq_read_dirs(&sq);
    Note *notes = NULL;
    int nn = notes_load(&notes);
    static ShEntry last[1];
    Names journey, em_games, em_earned;
    names_load(&journey, CP_JOURNEY_DIR);
    names_load(&em_games, EM_DIR "/games");
    names_load(&em_earned, EM_DIR "/earned");
    for (int i = 0; i < lib->n; i++) {
        const ShGame *g = &lib->g[i];
        ShInfo *in = &info[i];
        memset(in, 0, sizeof(*in));
        in->sq_newest = -1;
        in->pic = -1;
        in->em_total = -1;
        if (g->type == GT_SKIP) continue;
        for (int k = 0; k < nsq; k++) {
            if (sh_casecmp(sq[k].boot, g->path)) continue;
            sh_strcpy(in->sq_dir, sizeof(in->sq_dir), sq[k].dir);
            for (int s = 0; s < 6; s++) {
                in->sq_has[s] = sq[k].has[s];
                in->sq_time[s] = sq[k].when[s];
                if (sq[k].has[s] && (in->sq_newest < 0 || sq[k].when[s] > in->sq_time[(int)in->sq_newest])) in->sq_newest = (s8)s;
            }
            break;
        }
        if (g->id[0]) {
            int total = 0;
            if (names_has(&journey, g->id, ".TXT") && data_journey(g->id, last, 1, &total) == 1) {
                sh_strcpy(in->cp_last, sizeof(in->cp_last), last[0].text);
                in->cp_last_time = last[0].when;
            }
            in->cp_entries = total;
            in->cp_secs = read_seconds(g->id, &journey);
            if (names_has(&em_games, g->id, ".txt")) emblem_read(g->id, in, names_has(&em_earned, g->id, ".txt"));
        }
        for (int k = 0; k < nn; k++)
            if (!strcmp(notes[k].path, g->path)) {
                in->launched = notes[k].launched;
                in->finished = notes[k].finished;
                in->hidden = (u8)(notes[k].hidden != 0);
                in->fav = (u8)(notes[k].fav != 0);
                in->pic = (s8)(notes[k].pic < -1 || notes[k].pic > 6 ? -1 : notes[k].pic);
                break;
            }
        data_finish(g, in);
    }
    (void)now;
    free(journey.name); free(em_games.name); free(em_earned.name);
    free(sq);
    free(notes);
}

/* ---- settings ------------------------------------------------------------------ */
void config_load(ShConfig *c)
{
    static char buf[2048];
    memset(c, 0, sizeof(*c));
    c->sort[0] = SORT_LAST; c->sort[1] = SORT_NAME; c->sort[2] = SORT_FINISHED; c->sort[3] = SORT_NAME;
    sh_strcpy(c->all_default, sizeof(c->all_default), "PSP Games");
    sh_strcpy(c->fav_default, sizeof(c->fav_default), "All Games");
    c->show_psn = c->show_homebrew = c->show_ps1 = 1;
    c->tab_home = c->tab_fav = c->tab_finished = 1;
    c->grid_cols = 6;
    c->open_tab = 3;
    c->confirm_exit = 1;
    c->return_to_shelf = 1;
    if (sh_read_file(SH_CONFIG_PATH, buf, sizeof(buf)) <= 0) return;
    for (char *l = buf; l && *l;) {
        char *next = strchr(l, '\n');
        if (next) *next++ = 0;
        char *eq = strchr(l, '=');
        if (eq && *l != '#') {
            *eq = 0;
            char *k = l; while (*k == ' ') k++;
            char *z = k + strlen(k); while (z > k && z[-1] == ' ') *--z = 0;
            int v = atoi(eq + 1);
            char *sv = eq + 1; while (*sv == ' ') sv++;
            char *sz = sv + strlen(sv); while (sz > sv && (sz[-1] == ' ' || sz[-1] == '\r')) *--sz = 0;
            if (!strcmp(k, "open_tab") && v >= 0 && v <= 4) c->open_tab = v;
            else if (!strcmp(k, "sort_favourites") && v >= 0 && v < SORT_COUNT) c->sort[3] = v;
            else if (!strcmp(k, "all_opens_on") && sv[0]) sh_strcpy(c->all_default, sizeof(c->all_default), sv);
            else if (!strcmp(k, "favourites_open_on") && sv[0]) sh_strcpy(c->fav_default, sizeof(c->fav_default), sv);
            else if (!strcmp(k, "last_tab") && v >= 0 && v <= 3) c->last_tab = v;
            else if (!strcmp(k, "last_category")) sh_strcpy(c->last_cat, sizeof(c->last_cat), sv);
            else if (!strcmp(k, "last_game")) sh_strcpy(c->last_game, sizeof(c->last_game), sv);
            else if (!strcmp(k, "confirm_exit")) c->confirm_exit = v != 0;
            else if (!strcmp(k, "return_to_shelf")) c->return_to_shelf = v != 0;
            else if (!strcmp(k, "sort_playing") && v >= 0 && v < SORT_COUNT) c->sort[0] = v;
            else if (!strcmp(k, "sort_all") && v >= 0 && v < SORT_COUNT) c->sort[1] = v;
            else if (!strcmp(k, "sort_finished") && v >= 0 && v < SORT_COUNT) c->sort[2] = v;
            else if (!strcmp(k, "show_psn")) c->show_psn = v != 0;
            else if (!strcmp(k, "show_homebrew")) c->show_homebrew = v != 0;
            else if (!strcmp(k, "show_ps1")) c->show_ps1 = v != 0;
            else if (!strcmp(k, "show_hidden")) c->show_hidden = v != 0;
            else if (!strcmp(k, "tab_home")) c->tab_home = v != 0;
            else if (!strcmp(k, "tab_favourites")) c->tab_fav = v != 0;
            else if (!strcmp(k, "tab_finished")) c->tab_finished = v != 0;
            else if (!strcmp(k, "titles_from_file")) c->titles_from_file = v != 0;
            else if (!strcmp(k, "pic_mode")) c->pic_mode = v != 0;
            else if (!strcmp(k, "grid_cols") && v >= 3 && v <= 6) c->grid_cols = v;
        }
        l = next;
    }
}

void config_save(const ShConfig *c)
{
    char buf[1024];
    int n = sh_snprintf(buf, sizeof(buf),
        "# Shelf settings (changed from Shelf's own settings screen)\r\n"
        "open_tab = %d\r\nsort_playing = %d\r\nsort_all = %d\r\nsort_finished = %d\r\n"
        "show_psn = %d\r\nshow_homebrew = %d\r\nshow_ps1 = %d\r\nshow_hidden = %d\r\ntitles_from_file = %d\r\npic_mode = %d\r\ngrid_cols = %d\r\n"
        "confirm_exit = %d\r\nreturn_to_shelf = %d\r\nlast_tab = %d\r\nlast_category = %s\r\nlast_game = %s\r\n"
        "sort_favourites = %d\r\nall_opens_on = %s\r\nfavourites_open_on = %s\r\n"
        "tab_home = %d\r\ntab_favourites = %d\r\ntab_finished = %d\r\n",
        c->open_tab, c->sort[0], c->sort[1], c->sort[2], c->show_psn, c->show_homebrew, c->show_ps1, c->show_hidden,
        c->titles_from_file, c->pic_mode, c->grid_cols, c->confirm_exit, c->return_to_shelf, c->last_tab, c->last_cat, c->last_game,
        c->sort[3], c->all_default, c->fav_default, c->tab_home, c->tab_fav, c->tab_finished);
    SceUID fd = sceIoOpen(SH_CONFIG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, buf, n);
    sceIoClose(fd);
}
