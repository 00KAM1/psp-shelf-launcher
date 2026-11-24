#ifndef SH_IMAGE_H
#define SH_IMAGE_H
#include <psptypes.h>

/* Pictures are kept as RGB565, the way Sidequest stores its slot pictures. */
typedef struct { int w, h; u16 *px; } ShImage;

#define ICON_W 144
#define ICON_H 80
#define SMALL_W 72
#define SMALL_H 40
#define PIC_W 240
#define PIC_H 136

/* PNG bytes -> picture fitted inside w x h (aspect kept, centred on black). 0 = ok. */
int  img_png_fit(const u8 *png, u32 len, int w, int h, u16 *out);
/* PNG bytes -> picture filling w x h (stretched to cover, as PIC1 is meant to). 0 = ok. */
int  img_png_fill(const u8 *png, u32 len, int w, int h, u16 *out);
/* PNG bytes -> a full-colour picture filling w x h, each colour at pct percent, written
 * straight into 32-bit pixels (stride in pixels). For the one picture shown full screen. 0 = ok. */
int  img_png_fill32(const u8 *png, u32 len, int w, int h, u32 *out, int stride, int pct);
/* Box-filter shrink (or nearest enlarge) of a 565 picture into dst w x h. */
void img_scale565(const u16 *src, int sw, int sh, u16 *dst, int dw, int dh);
/* A Sidequest .THM ("SQT1", w, h, RGB565) scaled to w x h. 0 = ok. */
int  img_read_thm(const char *path, u16 *out, int w, int h);
#endif
