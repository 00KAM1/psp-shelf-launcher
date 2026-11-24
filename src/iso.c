/*
 * Shelf - reading files out of a game image: plain ISO, CSO (v1 and v2) and
 * ZSO. Only what the library needs: PARAM.SFO, ICON0.PNG and PIC1.PNG.
 * Blocks are read one at a time and their index entries looked up on demand,
 * so nothing big is ever held in memory.
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "iso.h"
#include "lodepng.h"

#define SECTOR 2048

static int rd_at(SceUID fd, u32 off, void *buf, int len)
{
    if (sceIoLseek32(fd, (int)off, PSP_SEEK_SET) != (int)off) return -1;
    return sceIoRead(fd, buf, len);
}

/* LZ4 block format, as ZSO and CSO v2 use it. Returns bytes written or -1. */
int sh_lz4_block(const u8 *src, int slen, u8 *dst, int dcap)
{
    const u8 *ip = src, *iend = src + slen;
    u8 *op = dst, *oend = dst + dcap;
    while (ip < iend) {
        u32 token = *ip++;
        u32 lit = token >> 4;
        if (lit == 15) { u8 b; do { if (ip >= iend) return -1; b = *ip++; lit += b; } while (b == 255); }
        if ((u32)(iend - ip) < lit || (u32)(oend - op) < lit) return -1;
        memcpy(op, ip, lit); op += lit; ip += lit;
        if (ip >= iend) break;                   /* the last sequence has no match */
        if (iend - ip < 2) return -1;
        u32 off = ip[0] | (ip[1] << 8); ip += 2;
        if (!off || off > (u32)(op - dst)) return -1;
        u32 mlen = token & 15;
        if (mlen == 15) { u8 b; do { if (ip >= iend) return -1; b = *ip++; mlen += b; } while (b == 255); }
        mlen += 4;
        if ((u32)(oend - op) < mlen) return -1;
        const u8 *m = op - off;
        for (u32 i = 0; i < mlen; i++) *op++ = m[i];
    }
    return (int)(op - dst);
}

static int inflate_raw(const u8 *src, int slen, u8 *dst, int dcap)
{
    unsigned char *out = NULL;
    size_t outsize = 0;
    LodePNGDecompressSettings s = lodepng_default_decompress_settings;
    s.max_output_size = dcap;
    unsigned e = lodepng_inflate(&out, &outsize, src, slen, &s);
    if (e && !out) return -1;
    int n = (int)(outsize < (size_t)dcap ? outsize : (size_t)dcap);
    if (out) { memcpy(dst, out, n); free(out); }
    return e && n == 0 ? -1 : n;
}

int iso_open(ShIso *iso, const char *path)
{
    memset(iso, 0, sizeof(*iso));
    iso->cur = 0xFFFFFFFFu;
    iso->fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (iso->fd < 0) return -1;
    u8 h[24];
    if (rd_at(iso->fd, 0, h, 24) != 24) { iso_close(iso); return -2; }
    if (!memcmp(h, "CISO", 4) || !memcmp(h, "ZISO", 4)) {
        iso->fmt = h[0] == 'Z' ? ISO_ZSO : ISO_CSO;
        u32 lo = h[8] | h[9] << 8 | h[10] << 16 | (u32)h[11] << 24;
        iso->total = lo;                          /* games are far below 4 GB */
        iso->block = h[16] | h[17] << 8 | h[18] << 16 | (u32)h[19] << 24;
        iso->ver = h[20];
        iso->align = h[21];
        if (iso->block < SECTOR || iso->block > 0x20000 || (iso->block & (iso->block - 1)) || iso->align > 16) { iso_close(iso); return -3; }
        iso->nblocks = (iso->total + iso->block - 1) / iso->block;
        iso->buf = malloc(iso->block);
        iso->cbuf = malloc(iso->block * 2 + 64);
        if (!iso->buf || !iso->cbuf) { iso_close(iso); return -4; }
    } else {
        iso->fmt = ISO_PLAIN;
        iso->total = sceIoLseek32(iso->fd, 0, PSP_SEEK_END);
        iso->block = SECTOR;
    }
    return 0;
}

void iso_close(ShIso *iso)
{
    if (iso->fd >= 0) sceIoClose(iso->fd);
    iso->fd = -1;
    free(iso->buf); free(iso->cbuf);
    iso->buf = iso->cbuf = NULL;
}

/* Fills iso->buf with block n of a compressed image. */
static int load_block(ShIso *iso, u32 n)
{
    if (n == iso->cur) return 0;
    if (n >= iso->nblocks) return -1;
    if (n < iso->ix_first || n >= iso->ix_first + iso->ix_n) {   /* one read covers the next 256 blocks */
        u32 want = iso->nblocks - n < ISO_IX_RUN ? iso->nblocks - n : ISO_IX_RUN;
        iso->ix_n = 0;
        if (rd_at(iso->fd, 24 + n * 4, iso->ix, (want + 1) * 4) != (int)((want + 1) * 4)) return -1;
        iso->ix_first = n; iso->ix_n = want;
    }
    u32 a = iso->ix[n - iso->ix_first], b = iso->ix[n - iso->ix_first + 1];
    u32 start = (a & 0x7FFFFFFFu) << iso->align, end = (b & 0x7FFFFFFFu) << iso->align;
    if (end <= start || end - start > iso->block * 2 + 64) {
        if (end - start == 0) return -1;
        if (end - start > iso->block * 2 + 64) end = start + iso->block * 2 + 64;
    }
    u32 clen = end - start;
    int r = rd_at(iso->fd, start, iso->cbuf, clen);
    if (r <= 0) return -1;
    int out;
    if (iso->fmt == ISO_CSO && iso->ver < 2 && (a & 0x80000000u)) {          /* v1: stored */
        out = r < (int)iso->block ? r : (int)iso->block; memcpy(iso->buf, iso->cbuf, out);
    } else if (iso->fmt == ISO_ZSO) {
        if (a & 0x80000000u) { out = r < (int)iso->block ? r : (int)iso->block; memcpy(iso->buf, iso->cbuf, out); }
        else out = sh_lz4_block(iso->cbuf, r, iso->buf, iso->block);
    } else if (iso->fmt == ISO_CSO && iso->ver >= 2) {
        if ((u32)r >= iso->block) { memcpy(iso->buf, iso->cbuf, iso->block); out = iso->block; }
        else if (a & 0x80000000u) out = sh_lz4_block(iso->cbuf, r, iso->buf, iso->block);
        else out = inflate_raw(iso->cbuf, r, iso->buf, iso->block);
    } else {
        out = inflate_raw(iso->cbuf, r, iso->buf, iso->block);
    }
    if (out < 0) return -1;
    if (out < (int)iso->block) memset(iso->buf + out, 0, iso->block - out);
    iso->cur = n;
    return 0;
}

int iso_read(ShIso *iso, u32 off, void *dst, u32 len)
{
    if (iso->fmt == ISO_PLAIN) return rd_at(iso->fd, off, dst, len) == (int)len ? 0 : -1;
    u8 *d = dst;
    while (len) {
        u32 n = off / iso->block, in = off % iso->block;
        if (load_block(iso, n) < 0) return -1;
        u32 take = iso->block - in < len ? iso->block - in : len;
        memcpy(d, iso->buf + in, take);
        d += take; off += take; len -= take;
    }
    return 0;
}

static u32 le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

/* Finds "PSP_GAME/ICON0.PNG": byte offset and size. 0 = found. */
int iso_find(ShIso *iso, const char *path, u32 *off, u32 *size)
{
    u8 pvd[SECTOR];
    if (iso_read(iso, 16 * SECTOR, pvd, SECTOR) < 0 || memcmp(pvd + 1, "CD001", 5)) return -1;
    u32 dir = le32(pvd + 156 + 2), dsize = le32(pvd + 156 + 10);
    const char *p = path;
    static u8 sec[SECTOR];
    while (*p) {
        const char *slash = strchr(p, '/');
        int len = slash ? (int)(slash - p) : (int)strlen(p);
        int found = 0;
        for (u32 s = 0; s < (dsize + SECTOR - 1) / SECTOR && s < 64 && !found; s++) {
            if (iso_read(iso, (dir + s) * SECTOR, sec, SECTOR) < 0) return -2;
            for (u32 o = 0; o < SECTOR;) {
                u8 rl = sec[o];
                if (!rl) break;                  /* rest of this sector is padding */
                if (o + rl > SECTOR || rl < 34) break;
                u8 nl = sec[o + 32];
                const char *nm = (const char *)sec + o + 33;
                int cmp = nl;
                for (int k = 0; k < nl; k++) if (nm[k] == ';') { cmp = k; break; }
                if (cmp == len) {
                    int same = 1;
                    for (int k = 0; k < len && same; k++) {
                        char a = nm[k], b = p[k];
                        if (a >= 'a' && a <= 'z') a -= 32;
                        if (b >= 'a' && b <= 'z') b -= 32;
                        same = a == b;
                    }
                    if (same) { dir = le32(sec + o + 2); dsize = le32(sec + o + 10); found = 1; break; }
                }
                o += rl;
            }
        }
        if (!found) return -3;
        p += len;
        if (*p == '/') p++;
    }
    *off = dir * SECTOR;
    *size = dsize;
    return 0;
}

u8 *iso_load(ShIso *iso, const char *path, u32 max, u32 *size)
{
    u32 off, sz;
    if (iso_find(iso, path, &off, &sz) < 0 || sz == 0 || sz > max) return NULL;
    u8 *b = malloc(sz + 1);
    if (!b) return NULL;
    if (iso_read(iso, off, b, sz) < 0) { free(b); return NULL; }
    b[sz] = 0;
    *size = sz;
    return b;
}
