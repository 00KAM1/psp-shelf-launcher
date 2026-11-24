/*
 * Shelf - decoding the games' pictures and scaling them.
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "image.h"
#include "lodepng.h"

static u16 to565(u32 r, u32 g, u32 b) { return (u16)((r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11)); }

/* Box-filtered sample of an RGBA picture over [x0,x1) x [y0,y1), alpha over black. */
static u16 box_rgba(const u8 *rgba, int sw, int x0, int x1, int y0, int y1)
{
    u32 r = 0, g = 0, b = 0, n = 0;
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const u8 *p = rgba + (y * sw + x) * 4;
            u32 a = p[3];
            if (a == 255) { r += p[0]; g += p[1]; b += p[2]; }
            else if (a) { r += p[0] * a / 255; g += p[1] * a / 255; b += p[2] * a / 255; }
            n++;
        }
    return to565(r / n, g / n, b / n);
}

static void place_rgba(const u8 *rgba, int sw, int sh, u16 *out, int ow, int oh, int dx, int dy, int dw, int dh)
{
    for (int y = 0; y < dh; y++) {
        int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
        if (y0 >= sh) y0 = sh - 1;
        if (y1 > sh) y1 = sh;
        for (int x = 0; x < dw; x++) {
            int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
            if (x0 >= sw) x0 = sw - 1;
            if (x1 > sw) x1 = sw;
            int ox = dx + x, oy = dy + y;
            if (ox >= 0 && oy >= 0 && ox < ow && oy < oh) out[oy * ow + ox] = box_rgba(rgba, sw, x0, x1, y0, y1);
        }
    }
}

static int decode(const u8 *png, u32 len, u8 **rgba, unsigned *w, unsigned *h)
{
    if (!png || len < 8) return -1;
    unsigned e = lodepng_decode32(rgba, w, h, png, len);
    if (e || !*rgba || !*w || !*h || *w > 2048 || *h > 2048) { free(*rgba); *rgba = NULL; return -1; }
    return 0;
}

int img_png_fit(const u8 *png, u32 len, int w, int h, u16 *out)
{
    u8 *rgba = NULL; unsigned sw, sh;
    if (decode(png, len, &rgba, &sw, &sh) < 0) return -1;
    memset(out, 0, w * h * 2);
    int dw = w, dh = (int)(sh * w / sw);
    if (dh > h) { dh = h; dw = (int)(sw * h / sh); }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    place_rgba(rgba, sw, sh, out, w, h, (w - dw) / 2, (h - dh) / 2, dw, dh);
    free(rgba);
    return 0;
}

int img_png_fill32(const u8 *png, u32 len, int w, int h, u32 *out, int stride, int pct)
{
    u8 *rgba = NULL; unsigned sw, sh;
    if (decode(png, len, &rgba, &sw, &sh) < 0) return -1;
    for (int y = 0; y < h; y++) {
        int y0 = y * (int)sh / h, y1 = (y + 1) * (int)sh / h;
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > (int)sh) { y1 = sh; if (y0 >= y1) y0 = y1 - 1; }
        for (int x = 0; x < w; x++) {
            int x0 = x * (int)sw / w, x1 = (x + 1) * (int)sw / w;
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > (int)sw) { x1 = sw; if (x0 >= x1) x0 = x1 - 1; }
            u32 r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const u8 *p = rgba + (yy * sw + xx) * 4;
                    u32 a = p[3];
                    r += p[0] * a / 255; g += p[1] * a / 255; b += p[2] * a / 255; n++;      /* alpha over black */
                }
            r = r / n * pct / 100; g = g / n * pct / 100; b = b / n * pct / 100;
            out[y * stride + x] = 0xFF000000u | (b << 16) | (g << 8) | r;
        }
    }
    free(rgba);
    return 0;
}

int img_png_fill(const u8 *png, u32 len, int w, int h, u16 *out)
{
    u8 *rgba = NULL; unsigned sw, sh;
    if (decode(png, len, &rgba, &sw, &sh) < 0) return -1;
    place_rgba(rgba, sw, sh, out, w, h, 0, 0, w, h);
    free(rgba);
    return 0;
}

void img_scale565(const u16 *src, int sw, int sh, u16 *dst, int dw, int dh)
{
    for (int y = 0; y < dh; y++) {
        int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
            if (x1 <= x0) x1 = x0 + 1;
            u32 r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1 && yy < sh; yy++)
                for (int xx = x0; xx < x1 && xx < sw; xx++) {
                    u16 p = src[yy * sw + xx];
                    r += p & 31; g += (p >> 5) & 63; b += p >> 11; n++;
                }
            if (!n) n = 1;
            dst[y * dw + x] = (u16)((r / n) | ((g / n) << 5) | ((b / n) << 11));
        }
    }
}

typedef struct { char magic[4]; u16 w, h; } ThumbHead;

int img_read_thm(const char *path, u16 *out, int w, int h)
{
    ThumbHead th;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    int ok = sceIoRead(fd, &th, sizeof(th)) == (int)sizeof(th) && !memcmp(th.magic, "SQT1", 4) &&
             th.w > 0 && th.h > 0 && th.w <= 480 && th.h <= 272;
    u16 *buf = ok ? malloc(th.w * th.h * 2) : NULL;
    if (buf) ok = sceIoRead(fd, buf, th.w * th.h * 2) == th.w * th.h * 2;
    sceIoClose(fd);
    if (!ok || !buf) { free(buf); return -1; }
    if (th.w == w && th.h == h) memcpy(out, buf, w * h * 2);
    else img_scale565(buf, th.w, th.h, out, w, h);
    free(buf);
    return 0;
}
