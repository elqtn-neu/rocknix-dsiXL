// gfx.h: the software renderer. Both screens are drawn into 32-bit surfaces at the DS's 256x192; the platform
// layer scales them up by whole pixels. Colours are 0x00RRGGBB in a surface; images carry straight alpha in the
// top byte (0xAARRGGBB).
#pragma once
#include "common.h"

typedef struct { int w, h, pitch; uint32_t *px; int cx0, cy0, cx1, cy1; } Surf;   /* clip: [cx0, cx1) x [cy0, cy1) */
typedef struct { int w, h; uint32_t *px; } Img;

#define RGB(r, g, b) ((uint32_t)((((r) & 255) << 16) | (((g) & 255) << 8) | ((b) & 255)))
#define ARGB(a, r, g, b) ((uint32_t)((((a) & 255) << 24) | (((r) & 255) << 16) | (((g) & 255) << 8) | ((b) & 255)))
#define CR(c) (((c) >> 16) & 255)
#define CG(c) (((c) >> 8) & 255)
#define CB(c) ((c) & 255)
#define CA(c) ((c) >> 24)

void surf_alloc(Surf *s, int w, int h);
void surf_free(Surf *s);
void surf_clip(Surf *s, int x, int y, int w, int h);
void surf_noclip(Surf *s);
void surf_copy(Surf *dst, const Surf *src);
/* a whole surface moved by (dx, dy) (screen shake), the uncovered edge filled with c */
void surf_shift(Surf *s, int dx, int dy, uint32_t c);

int img_alloc(Img *im, int w, int h);
void img_free(Img *im);

uint32_t col_mix(uint32_t a, uint32_t b, int t);          /* t 0..256: a -> b (alpha channel mixed too) */
uint32_t col_scale(uint32_t c, int k);                    /* 256 = 1.0, saturating */
uint32_t col_add(uint32_t a, uint32_t b);                 /* saturating add */
uint32_t col_desat(uint32_t c, int amt);                  /* amt 0..256 toward its own grey */
int col_luma(uint32_t c);

static inline void pblend(Surf *s, int x, int y, uint32_t c, int a) {
    if (x < s->cx0 || y < s->cy0 || x >= s->cx1 || y >= s->cy1 || a <= 0) return;
    uint32_t *p = s->px + (size_t)y * s->pitch + x;
    if (a >= 255) { *p = c & 0xffffff; return; }
    uint32_t d = *p, k = (uint32_t)a + ((uint32_t)a >> 7), ik = 256 - k;   /* 0..256 */
    uint32_t rb = (((c & 0xff00ff) * k + (d & 0xff00ff) * ik) >> 8) & 0xff00ff;
    uint32_t g = (((c & 0x00ff00) * k + (d & 0x00ff00) * ik) >> 8) & 0x00ff00;
    *p = rb | g;
}
static inline void pset(Surf *s, int x, int y, uint32_t c) {
    if (x >= s->cx0 && y >= s->cy0 && x < s->cx1 && y < s->cy1) s->px[(size_t)y * s->pitch + x] = c & 0xffffff;
}

void fill(Surf *s, uint32_t c);
void rectf(Surf *s, int x, int y, int w, int h, uint32_t c);
void rect_blend(Surf *s, int x, int y, int w, int h, uint32_t c, int a);
void rect_vgrad(Surf *s, int x, int y, int w, int h, uint32_t top, uint32_t bot, int a);
void hline(Surf *s, int x0, int x1, int y, uint32_t c, int a);
void vline(Surf *s, int x, int y0, int y1, uint32_t c, int a);
/* rounded rectangles with antialiased corners (float coordinates: they move by sub-pixels and stay smooth) */
float rrect_cov(float px, float py, float x, float y, float w, float h, float r);   /* coverage of a pixel centre */
void rrect(Surf *s, float x, float y, float w, float h, float r, uint32_t c, int a);
void rrect_vgrad(Surf *s, float x, float y, float w, float h, float r, uint32_t top, uint32_t bot, int a);
void rrect_line(Surf *s, float x, float y, float w, float h, float r, float t, uint32_t c, int a);   /* t: thickness */
void ellipse_soft(Surf *s, float cx, float cy, float rx, float ry, uint32_t c, int a);   /* soft-edged (shadows, glows) */
void disc(Surf *s, float cx, float cy, float r, uint32_t c, int a);                       /* antialiased */
void tri_fill(Surf *s, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c, int a);

/* images (straight alpha); alpha 0..255 overall */
void blit(Surf *s, const Img *im, int x, int y, int alpha);
void blit_region(Surf *s, const Img *im, int sx, int sy, int sw, int sh, int dx, int dy, int alpha);
/* scaled to dw x dh at a sub-pixel position, bilinear (smooth=1) or nearest */
void blit_scaled(Surf *s, const Img *im, float dx, float dy, float dw, float dh, int alpha, int smooth);
/* tinted: every opaque pixel becomes c (silhouettes, shadows), alpha by the image's own */
void blit_tint(Surf *s, const Img *im, float dx, float dy, float dw, float dh, uint32_t c, int alpha);

/* image helpers */
void img_fill(Img *im, uint32_t argb);
void img_draw(Img *dst, const Img *src, int x, int y);                         /* alpha-over */
void img_resize_area(const Img *src, Img *dst, int w, int h);                  /* box-filtered downscale */
void img_rotate(const Img *src, float angle, Img *dst);                        /* radians; smooth, soft edges */
void img_resize_cover(const Img *src, Img *dst, int w, int h, float focus_y);  /* fill w x h, cropping the excess */
