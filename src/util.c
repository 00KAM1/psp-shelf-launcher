/*
 * Shelf - small helpers: text, dates, CRC, files, the log.
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "common.h"

int sh_snprintf(char *buf, int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

void sh_log(const char *fmt, ...)
{
    char line[300];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 3) n = sizeof(line) - 3;
    line[n++] = '\r'; line[n++] = '\n'; line[n] = 0;
    SceUID fd = sceIoOpen(SH_LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, line, n); sceIoClose(fd); }
}

void sh_strcpy(char *dst, int size, const char *src)
{
    int i = 0;
    if (size <= 0) return;
    for (; src && src[i] && i < size - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

static u32 g_crc_table[256];
u32 sh_crc32(u32 crc, const void *data, u32 len)
{
    if (!g_crc_table[1])
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            g_crc_table[i] = c;
        }
    const u8 *p = data;
    crc = ~crc;
    while (len--) crc = g_crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int sh_casecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = lower((u8)*a), y = lower((u8)*b);
        if (x != y || !x) return x - y;
    }
}

int sh_ends_with(const char *s, const char *suffix)
{
    int n = strlen(s), m = strlen(suffix);
    return n >= m && !sh_casecmp(s + n - m, suffix);
}

/* ---- dates: minutes since 2000-01-01 00:00 ---------------------------------- */
static int leap(int y) { return (y % 4 == 0 && y % 100) || y % 400 == 0; }
static const int g_mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

u32 sh_minutes(int y, int mo, int d, int h, int mi)
{
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    u32 days = 0;
    for (int k = 2000; k < y; k++) days += leap(k) ? 366 : 365;
    for (int k = 1; k < mo; k++) days += g_mdays[k - 1] + (k == 2 && leap(y));
    days += d - 1;
    return days * 1440 + h * 60 + mi;
}

static void from_minutes(u32 m, int *y, int *mo, int *d, int *h, int *mi, int *wday)
{
    u32 days = m / 1440, rem = m % 1440;
    *h = rem / 60; *mi = rem % 60;
    *wday = (6 + days) % 7;                      /* 2000-01-01 was a Saturday */
    int yy = 2000;
    for (;;) { u32 n = leap(yy) ? 366 : 365; if (days < n) break; days -= n; yy++; }
    int mm = 1;
    for (;;) { u32 n = g_mdays[mm - 1] + (mm == 2 && leap(yy)); if (days < n) break; days -= n; mm++; }
    *y = yy; *mo = mm; *d = days + 1;
}

static int num(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) { if (s[i] < '0' || s[i] > '9') return -1; v = v * 10 + s[i] - '0'; }
    return v;
}

u32 sh_parse_stamp(const char *s)
{
    /* YYYY-MM-DD HH:MM */
    if (strlen(s) < 16 || s[4] != '-' || s[7] != '-' || s[10] != ' ' || s[13] != ':') return 0;
    int y = num(s, 4), mo = num(s + 5, 2), d = num(s + 8, 2), h = num(s + 11, 2), mi = num(s + 14, 2);
    if (y < 0 || mo < 0 || d < 0 || h < 0 || h > 23 || mi < 0 || mi > 59) return 0;
    return sh_minutes(y, mo, d, h, mi);
}

void sh_format_ago(u32 now, u32 then, char *out, int size)
{
    if (!then) { sh_snprintf(out, size, "never"); return; }
    if (then > now) then = now;
    u32 d = now - then;
    if (d < 60) sh_snprintf(out, size, "just now");
    else if (d < 1440) sh_snprintf(out, size, "%d hour%s ago", (int)(d / 60), d / 60 == 1 ? "" : "s");
    else if (d < 2 * 1440) sh_snprintf(out, size, "yesterday");
    else if (d < 14 * 1440) sh_snprintf(out, size, "%d days ago", (int)(d / 1440));
    else if (d < 60 * 1440) sh_snprintf(out, size, "%d weeks ago", (int)(d / (7 * 1440)));
    else if (d < 365 * 1440) sh_snprintf(out, size, "%d months ago", (int)(d / (30 * 1440)));
    else sh_snprintf(out, size, "over a year ago");
}

void sh_format_played(u32 secs, char *out, int size)
{
    sh_snprintf(out, size, "%dh %02dm", (int)(secs / 3600), (int)(secs / 60 % 60));
}

void sh_format_date(u32 minutes, char *out, int size, int with_time)
{
    static const char *const M[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char *const W[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    int y, mo, d, h, mi, w;
    if (!minutes) { sh_snprintf(out, size, "-"); return; }
    from_minutes(minutes, &y, &mo, &d, &h, &mi, &w);
    if (with_time) sh_snprintf(out, size, "%s %s %d %02d:%02d", W[w], M[mo - 1], d, h, mi);
    else sh_snprintf(out, size, "%s %d", M[mo - 1], d);
}

/* Copies text, replacing each character outside plain ASCII with one '?'.
 * Returns 1 if there were none. */
int sh_utf8_ascii(const char *in, char *out, int size)
{
    int o = 0, plain = 1;
    const u8 *p = (const u8 *)in;
    while (*p && o < size - 1) {
        if (*p < 0x80) { out[o++] = (*p >= 0x20 && *p < 0x7F) ? (char)*p : ' '; p++; continue; }
        int len = (*p >= 0xF0) ? 4 : (*p >= 0xE0) ? 3 : (*p >= 0xC0) ? 2 : 1;
        /* the full-width space and the common marks become their plain forms */
        if (len == 3 && p[0] == 0xE3 && p[1] == 0x80 && p[2] == 0x80) { out[o++] = ' '; plain = 0; }
        else if (len == 3 && p[0] == 0xE2 && p[1] == 0x84 && p[2] == 0xA2) { /* (TM): dropped */ }
        else if (len == 2 && p[0] == 0xC2 && (p[1] == 0xAE || p[1] == 0xA9)) { /* (R) (C): dropped */ }
        else { out[o++] = '?'; plain = 0; }
        for (int i = 0; i < len && *p; i++) p++;
    }
    out[o] = 0;
    return plain;
}

/* A file or folder name made readable: no folder, no extension, no
 * "(USA)" / "[T-En v1.0]" tags, underscores to spaces, spaces tidied. */
void sh_tidy_name(const char *file, char *out, int size)
{
    const char *name = strrchr(file, '/');
    name = name ? name + 1 : file;
    char tmp[SH_PATH_MAX];
    sh_strcpy(tmp, sizeof(tmp), name);
    char *dot = strrchr(tmp, '.');
    if (dot && dot != tmp && (sh_ends_with(tmp, ".iso") || sh_ends_with(tmp, ".cso") || sh_ends_with(tmp, ".zso") ||
                              sh_ends_with(tmp, ".dax") || sh_ends_with(tmp, ".pbp"))) *dot = 0;
    int o = 0, depth = 0, space = 0;
    for (const char *c = tmp; *c && o < size - 1; c++) {
        if (*c == '(' || *c == '[') { depth++; continue; }
        if ((*c == ')' || *c == ']') && depth) { depth--; continue; }
        if (depth) continue;
        char ch = (*c == '_') ? ' ' : *c;
        if (ch == ' ') { space = 1; continue; }
        if (space && o) out[o++] = ' ';
        space = 0;
        if (o < size - 1) out[o++] = ch;
    }
    out[o] = 0;
    if (!o) sh_strcpy(out, size, tmp);           /* the name was all tags: keep it whole */
}

int sh_read_file(const char *path, char *buf, int size)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    int n = sceIoRead(fd, buf, size - 1);
    sceIoClose(fd);
    if (n < 0) return -2;
    buf[n] = 0;
    return n;
}

int sh_mkdirs(void)
{
    sceIoMkdir(SH_DIR, 0777);
    sceIoMkdir(SH_CACHE_DIR, 0777);
    return 0;
}
