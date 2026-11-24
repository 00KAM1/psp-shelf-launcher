#include <string.h>
#include "track.h"

int ga_put(char *out, int size, int pos, const char *s)
{
    for (; *s; s++) { if (pos < size - 1) out[pos] = *s; pos++; }
    if (size > 0) out[pos < size ? pos : size - 1] = 0;
    return pos;
}

int ga_num(char *out, int size, int pos, gu32 v, int width)
{
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n < width && n < 11) t[n++] = '0';
    char s[12]; int k = 0;
    while (n) s[k++] = t[--n];
    s[k] = 0;
    return ga_put(out, size, pos, s);
}

static int put_pct(char *out, int size, int pos, int pct)
{
    if (pct < 0) return ga_put(out, size, pos, "-1");
    return ga_num(out, size, pos, (gu32)pct, 0);
}

static const unsigned char g_md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

gu32 ga_minutes(int y, int mo, int d, int h, int mi)
{
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    gu32 days = 0;
    for (int k = 2000; k < y; k++) days += leap(k) ? 366 : 365;
    for (int k = 1; k < mo; k++) days += g_md[k - 1] + (k == 2 && leap(y));
    days += d - 1;
    return days * 1440 + h * 60 + mi;
}

void ga_date(gu32 m, char *out, int size)
{
    static const char *const M[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char *const W[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    if (!m) { ga_put(out, size, 0, "-"); return; }
    gu32 days = m / 1440, rem = m % 1440;
    int w = (6 + days) % 7, y = 2000, mo = 1;
    for (;;) { gu32 n = leap(y) ? 366 : 365; if (days < n) break; days -= n; y++; }
    for (;;) { gu32 n = g_md[mo - 1] + (mo == 2 && leap(y)); if (days < n) break; days -= n; mo++; }
    int p = ga_put(out, size, 0, W[w]);
    p = ga_put(out, size, p, " "); p = ga_put(out, size, p, M[mo - 1]);
    p = ga_put(out, size, p, " "); p = ga_num(out, size, p, days + 1, 0);
    p = ga_put(out, size, p, " "); p = ga_num(out, size, p, rem / 60, 2);
    p = ga_put(out, size, p, ":"); ga_num(out, size, p, rem % 60, 2);
}

/* a whole number, or `none` when there is none; never reads past the text's end */
static const char *field(const char *p, long *v, int *got)
{
    while (*p == ' ' || *p == '\t') p++;
    int neg = 0, n = 0; long x = 0;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9' && n < 10) { x = x * 10 + (*p++ - '0'); n++; }
    while (*p >= '0' && *p <= '9') p++;
    *got = n > 0;
    *v = neg ? -x : x;
    return p;
}

static int pct_ok(long v) { return v >= 0 && v <= 100 ? (int)v : -1; }

void ga_parse(const char *text, GaRec *r)
{
    long v[7] = { 0, -1, 0, -1, 0, 0, -1 }; int got = 0, n = 0;
    memset(r, 0, sizeof(*r));
    r->start_pct = r->seen_pct = r->mark_pct = -1;
    if (!text) return;
    const char *p = text;
    for (; n < 7; n++) { long x; p = field(p, &x, &got); if (!got) break; v[n] = x; }
    if (n < 5 || v[0] <= 0 || v[2] < v[0]) return;            /* Shelf's own five fields at least */
    r->start = (gu32)v[0]; r->start_pct = pct_ok(v[1]);
    r->seen = (gu32)v[2];  r->seen_pct = pct_ok(v[3]);
    r->owner = (int)v[4];
    r->asleep = v[5] > 0 ? (gu32)v[5] : 0;
    if (r->asleep > r->seen - r->start) r->asleep = r->seen - r->start;
    r->mark_pct = pct_ok(v[6]);
    r->ok = 1;
}

int ga_format(const GaRec *r, char *out, int size)
{
    int p = ga_num(out, size, 0, r->start, 0);
    p = ga_put(out, size, p, " "); p = put_pct(out, size, p, r->start_pct);
    p = ga_put(out, size, p, " "); p = ga_num(out, size, p, r->seen, 0);
    p = ga_put(out, size, p, " "); p = put_pct(out, size, p, r->seen_pct);
    p = ga_put(out, size, p, " "); p = ga_num(out, size, p, GA_OWNER, 0);
    p = ga_put(out, size, p, " "); p = ga_num(out, size, p, r->asleep, 0);
    p = ga_put(out, size, p, " "); p = put_pct(out, size, p, r->mark_pct);
    return ga_put(out, size, p, "\r\n");
}

static int put_span(char *out, int size, int p, gu32 mins)
{
    p = ga_num(out, size, p, mins / 60, 0); p = ga_put(out, size, p, "h ");
    p = ga_num(out, size, p, mins % 60, 2); return ga_put(out, size, p, "m");
}

/* the line for a power-on that is over: the same shape Shelf writes, plus the sleep */
static void ended_line(const GaRec *r, char *out, int size)
{
    char a[24], b[24];
    gu32 all = r->seen - r->start, on = all - r->asleep;
    ga_date(r->start, a, sizeof(a)); ga_date(r->seen, b, sizeof(b));
    int p = ga_put(out, size, 0, a);
    p = ga_put(out, size, p, " to "); p = ga_put(out, size, p, b);
    p = ga_put(out, size, p, " | "); p = put_span(out, size, p, on);
    if (r->asleep) { p = ga_put(out, size, p, " on, "); p = put_span(out, size, p, r->asleep); p = ga_put(out, size, p, " asleep"); }
    if (r->start_pct >= 0 && r->seen_pct >= 0) {
        int d = r->seen_pct - r->start_pct;
        p = ga_put(out, size, p, " | battery "); p = ga_num(out, size, p, (gu32)r->start_pct, 0);
        p = ga_put(out, size, p, "% to ");       p = ga_num(out, size, p, (gu32)r->seen_pct, 0);
        p = ga_put(out, size, p, "% | ");        p = ga_put(out, size, p, d > 0 ? "+" : "-");
        p = ga_num(out, size, p, (gu32)(d < 0 ? -d : d), 0); ga_put(out, size, p, "%");
    } else ga_put(out, size, p, " | no battery reading");
}

int ga_step(GaRec *r, gu32 now, int pct, int first, char *ended, int size)
{
    int what = GA_SAME;
    if (size > 0) ended[0] = 0;
    if (pct < 0 || pct > 100) pct = -1;
    if (!now) return GA_SAME;                                  /* no clock reading: leave the record alone */

    if (r->ok && now < r->seen) {                              /* the clock was set back */
        gu32 back = r->seen - now;
        r->start = r->start > back ? r->start - back : 1;
        r->seen = now;
    }
    gu32 gap = r->ok ? now - r->seen : 0;
    if (!r->ok || (gap > GA_GAP && first)) {
        /* Nothing looked for a while and this plugin has only just been loaded: the PSP was off. */
        if (r->ok && r->seen > r->start) ended_line(r, ended, size);
        memset(r, 0, sizeof(*r));
        r->ok = 1; r->start = now; r->start_pct = pct; r->mark_pct = -1;
        what = GA_NEW;
    } else if (gap > GA_GAP) {
        /* Nothing looked for a while but this plugin was in memory all along: the PSP was asleep. */
        r->asleep += gap;
        what = GA_WOKE;
    }
    r->seen = now; r->seen_pct = pct; r->owner = GA_OWNER;
    return what;
}

int ga_use_line(GaRec *r, gu32 now, int pct, int charging, int what, const char *program, char *out, int size)
{
    const char *why = NULL;
    if (pct < 0 || pct > 100) pct = -1;
    if (what == GA_NEW) why = "power on";
    else if (what == GA_WOKE) why = "woke";
    else if (program) why = "start";
    else if (pct >= 0 && r->mark_pct >= 0 && (pct >= r->mark_pct + GA_STEP || pct + GA_STEP <= r->mark_pct)) why = "";
    else if (pct >= 0 && r->mark_pct < 0) why = "";
    if (!why) return 0;
    char d[24];
    ga_date(now, d, sizeof(d));
    int p = ga_put(out, size, 0, d);
    p = ga_put(out, size, p, " | ");
    if (pct >= 0) { if (pct < 100) p = ga_put(out, size, p, pct < 10 ? "  " : " "); p = ga_num(out, size, p, (gu32)pct, 0); p = ga_put(out, size, p, "%"); }
    else p = ga_put(out, size, p, "  --");
    p = ga_put(out, size, p, charging ? " charging" : "         ");
    if (*why) { p = ga_put(out, size, p, " | "); p = ga_put(out, size, p, why); }
    if (program) { p = ga_put(out, size, p, *why ? " " : " | "); p = ga_put(out, size, p, program); }
    while (p > 0 && p <= size - 1 && out[p - 1] == ' ') out[--p] = 0;
    if (pct >= 0) r->mark_pct = pct;
    return 1;
}
