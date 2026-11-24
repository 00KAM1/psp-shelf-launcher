/* Shelf - PARAM.SFO values. Strings are copied as they are (UTF-8);
 * numbers are written out in decimal. */
#include <string.h>
#include <stdio.h>
#include "common.h"
#include "iso.h"

static u32 le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u16 le16(const u8 *p) { return p[0] | p[1] << 8; }

int sfo_get(const u8 *sfo, u32 size, const char *key, char *out, int outsize)
{
    if (!sfo || size < 20 || memcmp(sfo, "\0PSF", 4) || outsize < 1) return -1;
    u32 keys = le32(sfo + 8), data = le32(sfo + 12), count = le32(sfo + 16);
    if (keys >= size || data >= size || count > 256 || 20 + count * 16 > size) return -1;
    for (u32 i = 0; i < count; i++) {
        const u8 *e = sfo + 20 + i * 16;
        u32 koff = keys + le16(e), fmt = le16(e + 2), len = le32(e + 4), doff = data + le32(e + 12);
        if (koff >= size || doff >= size) return -1;
        const char *k = (const char *)sfo + koff;
        u32 kmax = size - koff, kl = 0;
        while (kl < kmax && k[kl]) kl++;
        if (kl == kmax || strcmp(k, key)) continue;
        if (fmt == 0x0404) {                      /* number */
            if (doff + 4 > size) return -1;
            snprintf(out, outsize, "%u", (unsigned)le32(sfo + doff));
            return 0;
        }
        if (len > size - doff) len = size - doff;
        int n = 0;
        for (u32 j = 0; j < len && sfo[doff + j] && n < outsize - 1; j++) out[n++] = (char)sfo[doff + j];
        out[n] = 0;
        return 0;
    }
    return -1;
}
