#ifndef SH_ISO_H
#define SH_ISO_H
#include <psptypes.h>

enum { ISO_PLAIN = 0, ISO_CSO, ISO_ZSO };
#define ISO_IX_RUN 256
typedef struct {
    int fd, fmt, ver, align;
    u32 total, block, nblocks, cur;
    u8 *buf, *cbuf;
    u32 ix[ISO_IX_RUN + 1], ix_first, ix_n;      /* a run of the block index */
} ShIso;

int  iso_open(ShIso *iso, const char *path);
void iso_close(ShIso *iso);
int  iso_read(ShIso *iso, u32 off, void *dst, u32 len);
int  iso_find(ShIso *iso, const char *path, u32 *off, u32 *size);
u8  *iso_load(ShIso *iso, const char *path, u32 max, u32 *size);   /* malloc'd, NUL after the end */
int  sh_lz4_block(const u8 *src, int slen, u8 *dst, int dcap);

/* sfo.c */
int sfo_get(const u8 *sfo, u32 size, const char *key, char *out, int outsize);   /* 0 = found (strings and numbers) */
#endif
