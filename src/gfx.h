#ifndef SH_GFX_H
#define SH_GFX_H
#include <psptypes.h>

#define SCR_W 480
#define SCR_H 272

/* A 32-bit (8888) surface, the way the PSP shows it: 0xAABBGGRR. */
typedef struct { u32 *px; int stride; } ShSurf;

#define RGB(r, g, b) (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

#define C_BG     RGB(14, 17, 22)
#define C_CARD   RGB(27, 32, 41)
#define C_CARD2  RGB(34, 40, 52)
#define C_PANEL  RGB(24, 28, 36)
#define C_BAR    RGB(22, 26, 34)
#define C_KEYBAR RGB(20, 24, 31)
#define C_LINE   RGB(48, 56, 70)
#define C_FRAME  RGB(52, 60, 74)
#define C_SEL    RGB(50, 58, 74)
#define C_WHITE  RGB(234, 236, 240)
#define C_GREY   RGB(128, 138, 152)
#define C_DIM    RGB(78, 86, 100)
#define C_AMBER  RGB(255, 176, 0)
#define C_RED    RGB(224, 84, 84)
#define C_GOLD   RGB(222, 184, 70)
#define C_GREEN  RGB(90, 196, 110)
#define C_BLUE   RGB(110, 150, 235)
#define C_PINK   RGB(230, 120, 190)
#define C_BLACK  RGB(0, 0, 0)
#define C_TRACK  RGB(40, 46, 58)

extern u8 msx[];                                 /* the 8x8 font from pspsdk's debug library */

/* text.c: UTF-8; one 8-pixel cell per character */
u32  u8_next(const char **ps);
int  u8_cols(const char *s);
int  u8_cut(const char *s, int cols, char *out, int size);
int  u8_bytes(const char *s, int cols);
const u8 *u8_glyph(u32 c);
void u8_copy(char *dst, int size, const char *src);

void gfx_rect(const ShSurf *s, int x, int y, int w, int h, u32 c);
void gfx_frame(const ShSurf *s, int x, int y, int w, int h, u32 c, int t);   /* outline, t px thick */
void gfx_hline(const ShSurf *s, int x, int y, int w, u32 c);
void gfx_vline(const ShSurf *s, int x, int y, int h, u32 c);
int  gfx_text(const ShSurf *s, int x, int y, const char *t, u32 c);      /* returns the width drawn */
int  gfx_text_n(const ShSurf *s, int x, int y, const char *t, int n, u32 c);
void gfx_text_r(const ShSurf *s, int right, int y, const char *t, u32 c);  /* right-aligned */
void gfx_text_c(const ShSurf *s, int cx, int y, const char *t, u32 c);     /* centred */
int  gfx_tw(const char *t);
void gfx_blit565(const ShSurf *s, int x, int y, const u16 *img, int w, int h);
void gfx_blit565_dim(const ShSurf *s, int x, int y, const u16 *img, int w, int h, int pct);
void gfx_blit565_scaled(const ShSurf *s, int x, int y, int dw, int dh, const u16 *img, int w, int h);
void gfx_dim(const ShSurf *s, int x, int y, int w, int h, int pct);
void gfx_bar(const ShSurf *s, int x, int y, int w, int h, int num, int den, u32 c);
/* PSP buttons: 'X' cross, 'O' circle, 'S' square, 'T' triangle; arrows 'U' 'D' 'L' 'R' */
void gfx_button(const ShSurf *s, int x, int y, char b);
void gfx_badge(const ShSurf *s, int x, int y, char kind);   /* 'P' paused, 'S' state flag, 'C' tick, 'D' dot, 'F' star */
void gfx_dot(const ShSurf *s, int x, int y, u32 c, int filled);
#endif
