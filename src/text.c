/*
 * Shelf - UTF-8 text: reading characters, counting and cutting by characters
 * (never inside one), and finding an 8x8 glyph. Plain ASCII comes from the
 * PSPSDK debug font, Japanese from the table in jfont_data.c (the set
 * Checkpoint uses), anything else is '?'. Every character is one 8-pixel cell.
 */
#include <string.h>
#include "gfx.h"

extern const int jfont_count, jfont_fold_count;
extern const unsigned short jfont_cp[], jfont_fold_from[], jfont_fold_to[];
extern const unsigned char jfont_bits[];

u32 u8_next(const char **ps)
{
    const u8 *p = (const u8 *)*ps;
    u32 c = *p;
    if (!c) return 0;
    int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (n < 0) { *ps = (const char *)p + 1; return '?'; }
    c &= n == 0 ? 0x7F : n == 1 ? 0x1F : n == 2 ? 0x0F : 0x07;
    for (int i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *ps = (const char *)p + 1; return '?'; }
        c = (c << 6) | (p[i] & 0x3F);
    }
    *ps = (const char *)p + 1 + n;
    return c;
}

int u8_cols(const char *s)
{
    int n = 0;
    while (*s) { u8_next(&s); n++; }
    return n;
}

/* Copies at most `cols` characters (and size-1 bytes) of s; if it had to cut,
 * the last character kept is replaced by '.'. Returns the characters copied. */
int u8_cut(const char *s, int cols, char *out, int size)
{
    int total = u8_cols(s), n = 0, o = 0;
    int limit = total > cols ? cols - 1 : cols;
    const char *p = s;
    while (*p && n < limit) {
        const char *q = p;
        u8_next(&q);
        int len = (int)(q - p);
        if (o + len >= size - 1) break;
        memcpy(out + o, p, len); o += len; n++;
        p = q;
    }
    if (total > cols && o < size - 1 && cols > 0) { out[o++] = '.'; n++; }
    out[o] = 0;
    return n;
}

/* The byte offset after `cols` characters. */
int u8_bytes(const char *s, int cols)
{
    const char *p = s;
    for (int i = 0; i < cols && *p; i++) u8_next(&p);
    return (int)(p - s);
}

const u8 *u8_glyph(u32 c)
{
    static const u8 qm[8] = { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00 };
    if (c < 0x80) return &msx[c * 8];
    for (int lo = 0, hi = jfont_fold_count - 1; lo <= hi;) {             /* half-width kana, accents */
        int m = (lo + hi) / 2;
        if (jfont_fold_from[m] == c) { c = jfont_fold_to[m]; break; }
        if (jfont_fold_from[m] < c) lo = m + 1; else hi = m - 1;
    }
    if (c < 0x80) return &msx[c * 8];
    for (int lo = 0, hi = jfont_count - 1; lo <= hi;) {
        int m = (lo + hi) / 2;
        if (jfont_cp[m] == c) return &jfont_bits[m * 8];
        if (jfont_cp[m] < c) lo = m + 1; else hi = m - 1;
    }
    return qm;
}

/* Copies UTF-8 text cleanly: control characters dropped, never cut inside a
 * character, at most size-1 bytes. */
void u8_copy(char *dst, int size, const char *src)
{
    int o = 0;
    const char *p = src;
    while (*p) {
        const char *q = p;
        u32 c = u8_next(&q);
        int len = (int)(q - p);
        if (o + len > size - 1) break;
        if (c >= 0x20 && c != 0x7F) { memcpy(dst + o, p, len); o += len; }
        p = q;
    }
    dst[o] = 0;
}
