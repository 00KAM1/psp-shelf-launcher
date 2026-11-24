/*
 * Shelf - drawing on a 480x272 32-bit surface. No PSP calls: the same code
 * draws the PC renders.
 */
#include <string.h>
#include "gfx.h"

static inline void pset(const ShSurf *s, int x, int y, u32 c)
{
    if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) s->px[y * s->stride + x] = c;
}

void gfx_rect(const ShSurf *s, int x, int y, int w, int h, u32 c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCR_W ? SCR_W : x + w, y1 = y + h > SCR_H ? SCR_H : y + h;
    for (int j = y0; j < y1; j++) {
        u32 *row = s->px + j * s->stride;
        for (int i = x0; i < x1; i++) row[i] = c;
    }
}

void gfx_hline(const ShSurf *s, int x, int y, int w, u32 c) { gfx_rect(s, x, y, w, 1, c); }
void gfx_vline(const ShSurf *s, int x, int y, int h, u32 c) { gfx_rect(s, x, y, 1, h, c); }

void gfx_frame(const ShSurf *s, int x, int y, int w, int h, u32 c, int t)
{
    gfx_rect(s, x, y, w, t, c);
    gfx_rect(s, x, y + h - t, w, t, c);
    gfx_rect(s, x, y, t, h, c);
    gfx_rect(s, x + w - t, y, t, h, c);
}

int gfx_tw(const char *t) { return u8_cols(t) * 8; }

int gfx_text_n(const ShSurf *s, int x, int y, const char *t, int n, u32 c)
{
    int x0 = x;
    const char *p = t;
    for (int k = 0; *p && k < n; k++, x += 8) {
        const u8 *g = u8_glyph(u8_next(&p));
        for (int r = 0; r < 8; r++) {
            u8 bits = g[r];
            if (!bits) continue;
            for (int col = 0; col < 8; col++) if (bits & (0x80 >> col)) pset(s, x + col, y + r, c);
        }
    }
    return x - x0;
}

int gfx_text(const ShSurf *s, int x, int y, const char *t, u32 c) { return gfx_text_n(s, x, y, t, 1 << 20, c); }
void gfx_text_r(const ShSurf *s, int right, int y, const char *t, u32 c) { gfx_text(s, right - gfx_tw(t), y, t, c); }
void gfx_text_c(const ShSurf *s, int cx, int y, const char *t, u32 c) { gfx_text(s, cx - gfx_tw(t) / 2, y, t, c); }

static inline u32 from565(u16 p)
{
    u32 r = (p & 31) << 3, g = ((p >> 5) & 63) << 2, b = (p >> 11) << 3;
    return RGB(r | r >> 5, g | g >> 6, b | b >> 5);
}

void gfx_blit565(const ShSurf *s, int x, int y, const u16 *img, int w, int h)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if ((unsigned)yy >= SCR_H) continue;
        u32 *row = s->px + yy * s->stride;
        const u16 *src = img + j * w;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if ((unsigned)xx < SCR_W) row[xx] = from565(src[i]);
        }
    }
}

void gfx_blit565_dim(const ShSurf *s, int x, int y, const u16 *img, int w, int h, int pct)
{
    gfx_blit565(s, x, y, img, w, h);
    gfx_dim(s, x, y, w, h, pct);
}

void gfx_blit565_scaled(const ShSurf *s, int x, int y, int dw, int dh, const u16 *img, int w, int h)
{
    for (int j = 0; j < dh; j++) {
        int y0 = j * h / dh, y1 = (j + 1) * h / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int i = 0; i < dw; i++) {
            int x0 = i * w / dw, x1 = (i + 1) * w / dw;
            if (x1 <= x0) x1 = x0 + 1;
            u32 r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1 && yy < h; yy++)
                for (int xx = x0; xx < x1 && xx < w; xx++) {
                    u16 p = img[yy * w + xx];
                    r += p & 31; g += (p >> 5) & 63; b += p >> 11; n++;
                }
            if (!n) continue;
            pset(s, x + i, y + j, from565((u16)((r / n) | ((g / n) << 5) | ((b / n) << 11))));
        }
    }
}

void gfx_dim(const ShSurf *s, int x, int y, int w, int h, int pct)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCR_W ? SCR_W : x + w, y1 = y + h > SCR_H ? SCR_H : y + h;
    u32 f = (u32)pct * 256 / 100;
    for (int j = y0; j < y1; j++) {
        u32 *row = s->px + j * s->stride;
        for (int i = x0; i < x1; i++) {
            u32 v = row[i];
            u32 r = (v & 0xFF) * f >> 8, g = ((v >> 8) & 0xFF) * f >> 8, b = ((v >> 16) & 0xFF) * f >> 8;
            row[i] = 0xFF000000u | b << 16 | g << 8 | r;
        }
    }
}

void gfx_bar(const ShSurf *s, int x, int y, int w, int h, int num, int den, u32 c)
{
    gfx_rect(s, x, y, w, h, C_TRACK);
    if (den > 0 && num > 0) {
        int f = (int)((long long)w * num / den);
        if (f < 1) f = 1;
        if (f > w) f = w;
        gfx_rect(s, x, y, f, h, c);
    }
}

static void bitmap8(const ShSurf *s, int x, int y, const u8 *rows, u32 c)
{
    for (int r = 0; r < 8; r++)
        for (int col = 0; col < 8; col++) if (rows[r] & (0x80 >> col)) pset(s, x + col, y + r, c);
}

void gfx_button(const ShSurf *s, int x, int y, char b)
{
    static const u8 cross[8] = { 0xC3, 0xE7, 0x7E, 0x3C, 0x3C, 0x7E, 0xE7, 0xC3 };
    static const u8 circle[8] = { 0x3C, 0x7E, 0xE7, 0xC3, 0xC3, 0xE7, 0x7E, 0x3C };
    static const u8 square[8] = { 0xFF, 0xFF, 0xC3, 0xC3, 0xC3, 0xC3, 0xFF, 0xFF };
    static const u8 tri[8] = { 0x18, 0x18, 0x3C, 0x24, 0x66, 0x42, 0xFF, 0xFF };
    static const u8 up[8] = { 0x00, 0x18, 0x3C, 0x7E, 0xFF, 0x00, 0x00, 0x00 };
    static const u8 down[8] = { 0x00, 0x00, 0x00, 0xFF, 0x7E, 0x3C, 0x18, 0x00 };
    static const u8 left[8] = { 0x08, 0x18, 0x38, 0x78, 0x38, 0x18, 0x08, 0x00 };
    static const u8 right[8] = { 0x10, 0x18, 0x1C, 0x1E, 0x1C, 0x18, 0x10, 0x00 };
    switch (b) {
    case 'X': bitmap8(s, x, y, cross, C_BLUE); break;
    case 'O': bitmap8(s, x, y, circle, C_RED); break;
    case 'S': bitmap8(s, x, y, square, C_PINK); break;
    case 'T': bitmap8(s, x, y, tri, C_GREEN); break;
    case 'U': bitmap8(s, x, y, up, C_WHITE); break;
    case 'D': bitmap8(s, x, y, down, C_WHITE); break;
    case 'L': bitmap8(s, x, y, left, C_WHITE); break;
    case 'R': bitmap8(s, x, y, right, C_WHITE); break;
    }
}

void gfx_badge(const ShSurf *s, int x, int y, char kind)
{
    gfx_rect(s, x, y, 9, 9, C_BLACK);
    if (kind == 'P') { gfx_rect(s, x + 2, y + 2, 2, 5, C_AMBER); gfx_rect(s, x + 5, y + 2, 2, 5, C_AMBER); }
    else if (kind == 'C') {
        static const u8 tick[8] = { 0x00, 0x01, 0x03, 0x86, 0xCC, 0x78, 0x30, 0x00 };
        bitmap8(s, x + 1, y + 1, tick, C_GREEN);
    } else if (kind == 'D') gfx_dot(s, x + 1, y + 1, C_WHITE, 1);
    else if (kind == 'S') {                      /* a flag: the newest slot is a state */
        static const u8 flag[8] = { 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x66, 0x42, 0x00 };
        bitmap8(s, x, y + 1, flag, C_BLUE);
    } else if (kind == 'F') {
        static const u8 star[8] = { 0x10, 0x10, 0x38, 0xFE, 0x7C, 0x38, 0x6C, 0x44 };
        bitmap8(s, x + 1, y, star, C_GOLD);
    }
}

void gfx_dot(const ShSurf *s, int x, int y, u32 c, int filled)
{
    static const u8 full[8] = { 0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C };
    static const u8 ring[8] = { 0x3C, 0x42, 0x81, 0x81, 0x81, 0x81, 0x42, 0x3C };
    bitmap8(s, x, y, filled ? full : ring, c);
}
