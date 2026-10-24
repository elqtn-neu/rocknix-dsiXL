// hgfx.h: drawing at the panels' own resolution, made for the CPU of an RG DS Plus: rounded rectangles that are solid
// spans with only their corners antialiased, soft shadows made once and kept, gradients by the row, vector icons
// (strokes and rings by distance, only over their own few pixels), fast scaled sprites, and sprites with alpha made
// from any drawing (drawn over black and over white).
#pragma once
#include "gfx.h"

/* rounded rectangles (whole pixels; r the corner radius); a 0..255 */
void hr_fill(Surf *s, int x, int y, int w, int h, int r, uint32_t c, int a);
void hr_vgrad(Surf *s, int x, int y, int w, int h, int r, uint32_t top, uint32_t bot, int a);
void hr_ring(Surf *s, int x, int y, int w, int h, int r, int t, uint32_t c, int a);     /* t: line width */
/* the soft shadow under a rounded rectangle: blur px wide, moved down by dy */
void hr_shadow(Surf *s, int x, int y, int w, int h, int r, int blur, int dy, int a);
/* a radial glow (a soft disc), for lights and the sleep's moon */
void h_glow(Surf *s, float cx, float cy, float rad, uint32_t c, int a);

/* icons: a round-capped stroke, a ring (w > 0) or a disc (w <= 0), a filled polygon (convex or not, even-odd) */
void hi_line(Surf *s, float x0, float y0, float x1, float y1, float w, uint32_t c, int a);
void hi_circle(Surf *s, float cx, float cy, float r, float w, uint32_t c, int a);
void hi_arc(Surf *s, float cx, float cy, float r, float a0, float a1, float w, uint32_t c, int a);   /* radians */
void hi_poly(Surf *s, const float *xy, int n, uint32_t c, int a);

/* sprites: straight alpha; nearest-neighbour scaled (fast: for things in motion) and a box-filtered copy at a size */
void hb_blit(Surf *s, const Img *im, int x, int y, int a);
void hb_scaled(Surf *s, const Img *im, float x, float y, float w, float h, int a);
/* an image with alpha from a drawing: draw() is called twice, onto black and onto white, into w x h */
typedef void (*hb_draw_fn)(Surf *s, void *ctx);
int hb_capture(Img *out, int w, int h, hb_draw_fn draw, void *ctx);
/* a picture shrunk or grown smoothly to w x h (cover crop when cover) */
void hb_fit(const Img *src, Img *dst, int w, int h, int cover);
/* a fast smooth upscale of a small opaque image over the whole surface rows [y0, y1) */
void hb_stretch(Surf *s, const Img *im, int y0, int y1);

/* two pictures' main colours: a vivid one and a deep one (for a game's backdrop and its accents) */
void h_main_colors(const Img *im, uint32_t *vivid, uint32_t *deep);
uint32_t h_mix(uint32_t a, uint32_t b, float t);
