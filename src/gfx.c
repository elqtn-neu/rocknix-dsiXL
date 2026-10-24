// gfx.c: the software renderer (see gfx.h).
#include "gfx.h"
#include <stdlib.h>

void surf_alloc(Surf *s, int w, int h) {
    s->w = w; s->h = h; s->pitch = w;
    s->px = calloc((size_t)w * h, 4);
    surf_noclip(s);
}
void surf_free(Surf *s) { free(s->px); memset(s, 0, sizeof *s); }
void surf_clip(Surf *s, int x, int y, int w, int h) {
    s->cx0 = MAX(0, x); s->cy0 = MAX(0, y); s->cx1 = MIN(s->w, x + w); s->cy1 = MIN(s->h, y + h);
    if (s->cx1 < s->cx0) s->cx1 = s->cx0;
    if (s->cy1 < s->cy0) s->cy1 = s->cy0;
}
void surf_noclip(Surf *s) { s->cx0 = 0; s->cy0 = 0; s->cx1 = s->w; s->cy1 = s->h; }
void surf_copy(Surf *dst, const Surf *src) {
    for (int y = 0; y < MIN(dst->h, src->h); y++)
        memcpy(dst->px + (size_t)y * dst->pitch, src->px + (size_t)y * src->pitch, (size_t)MIN(dst->w, src->w) * 4);
}
void surf_shift(Surf *s, int dx, int dy, uint32_t c) {
    if (!dx && !dy) return;
    uint32_t *tmp = malloc((size_t)s->w * s->h * 4);
    if (!tmp) return;
    for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) {
        int sx = x - dx, sy = y - dy;
        tmp[(size_t)y * s->w + x] = (sx >= 0 && sy >= 0 && sx < s->w && sy < s->h) ? s->px[(size_t)sy * s->pitch + sx] : c;
    }
    for (int y = 0; y < s->h; y++) memcpy(s->px + (size_t)y * s->pitch, tmp + (size_t)y * s->w, (size_t)s->w * 4);
    free(tmp);
}

int img_alloc(Img *im, int w, int h) {
    im->w = w; im->h = h;
    im->px = calloc((size_t)MAX(1, w) * MAX(1, h), 4);
    return im->px ? 0 : -1;
}
void img_free(Img *im) { free(im->px); memset(im, 0, sizeof *im); }

uint32_t col_mix(uint32_t a, uint32_t b, int t) {
    t = CLAMP(t, 0, 256);
    int ia = (int)CA(a), ib = (int)CA(b);
    return ARGB(ia + (((ib - ia) * t) >> 8), (int)CR(a) + ((((int)CR(b) - (int)CR(a)) * t) >> 8),
                (int)CG(a) + ((((int)CG(b) - (int)CG(a)) * t) >> 8), (int)CB(a) + ((((int)CB(b) - (int)CB(a)) * t) >> 8));
}
uint32_t col_scale(uint32_t c, int k) {
    int r = MIN(255, (int)(CR(c) * k) >> 8), g = MIN(255, (int)(CG(c) * k) >> 8), b = MIN(255, (int)(CB(c) * k) >> 8);
    return (c & 0xff000000u) | RGB(r, g, b);
}
uint32_t col_add(uint32_t a, uint32_t b) {
    return (a & 0xff000000u) | RGB(MIN(255, CR(a) + CR(b)), MIN(255, CG(a) + CG(b)), MIN(255, CB(a) + CB(b)));
}
int col_luma(uint32_t c) { return (int)(CR(c) * 77 + CG(c) * 151 + CB(c) * 28) >> 8; }
uint32_t col_desat(uint32_t c, int amt) {
    int l = col_luma(c);
    return col_mix(c, (c & 0xff000000u) | RGB(l, l, l), amt);
}

void fill(Surf *s, uint32_t c) {
    for (int y = 0; y < s->h; y++) { uint32_t *r = s->px + (size_t)y * s->pitch; for (int x = 0; x < s->w; x++) r[x] = c; }
}
void rectf(Surf *s, int x, int y, int w, int h, uint32_t c) {
    int x0 = MAX(x, s->cx0), y0 = MAX(y, s->cy0), x1 = MIN(x + w, s->cx1), y1 = MIN(y + h, s->cy1);
    for (int yy = y0; yy < y1; yy++) { uint32_t *r = s->px + (size_t)yy * s->pitch; for (int xx = x0; xx < x1; xx++) r[xx] = c & 0xffffff; }
}
void rect_blend(Surf *s, int x, int y, int w, int h, uint32_t c, int a) {
    if (a <= 0) return;
    if (a >= 255) { rectf(s, x, y, w, h, c); return; }
    int x0 = MAX(x, s->cx0), y0 = MAX(y, s->cy0), x1 = MIN(x + w, s->cx1), y1 = MIN(y + h, s->cy1);
    /* the colour's share worked out once: per pixel two multiplies and an add */
    uint32_t k = (uint32_t)a + ((uint32_t)a >> 7), ik = 256 - k;
    uint32_t crb = (c & 0xff00ff) * k, cg = (c & 0x00ff00) * k;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *p = s->px + (size_t)yy * s->pitch;
        for (int xx = x0; xx < x1; xx++) {
            uint32_t d = p[xx];
            p[xx] = (((crb + (d & 0xff00ff) * ik) >> 8) & 0xff00ff) | (((cg + (d & 0x00ff00) * ik) >> 8) & 0x00ff00);
        }
    }
}
void rect_vgrad(Surf *s, int x, int y, int w, int h, uint32_t top, uint32_t bot, int a) {
    int x0 = MAX(x, s->cx0), x1 = MIN(x + w, s->cx1);
    for (int yy = 0; yy < h; yy++) {
        int py = y + yy;
        if (py < s->cy0 || py >= s->cy1) continue;
        uint32_t c = col_mix(top, bot, h > 1 ? yy * 256 / (h - 1) : 0);
        if (a >= 255) { uint32_t v = c & 0xffffff, *p = s->px + (size_t)py * s->pitch; for (int xx = x0; xx < x1; xx++) p[xx] = v; }
        else rect_blend(s, x0, py, x1 - x0, 1, c, a);
    }
}
void hline(Surf *s, int x0, int x1, int y, uint32_t c, int a) { for (int x = x0; x <= x1; x++) pblend(s, x, y, c, a); }
void vline(Surf *s, int x, int y0, int y1, uint32_t c, int a) { for (int y = y0; y <= y1; y++) pblend(s, x, y, c, a); }

/* coverage of the pixel centred at (px, py) by a rounded rectangle: 0..1 (a signed distance, one pixel of ramp) */
static inline float rr_cov(float px, float py, float x, float y, float w, float h, float r) {
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    r = MIN(r, MIN(hw, hh));
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = MAX(qx, 0), oy = MAX(qy, 0);
    float d = sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0) - r;
    return clampf(0.5f - d, 0, 1);
}

float rrect_cov(float px, float py, float x, float y, float w, float h, float r) { return rr_cov(px, py, x, y, w, h, r); }

void rrect_vgrad(Surf *s, float x, float y, float w, float h, float r, uint32_t top, uint32_t bot, int a) {
    if (w <= 0 || h <= 0 || a <= 0) return;
    int x0 = MAX((int)floorf(x), s->cx0), y0 = MAX((int)floorf(y), s->cy0);
    int x1 = MIN((int)ceilf(x + w), s->cx1), y1 = MIN((int)ceilf(y + h), s->cy1);
    for (int py = y0; py < y1; py++) {
        uint32_t c = top == bot ? top : col_mix(top, bot, (int)(sat((py + 0.5f - y) / h) * 256));
        float fy = py + 0.5f;
        /* the rows away from the corners are solid in the middle: only the ends need the distance */
        int inner = fy > y + r + 0.5f && fy < y + h - r - 0.5f;
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f;
            float cov = (inner && fx > x + 0.5f && fx < x + w - 0.5f) ? 1.0f : rr_cov(fx, fy, x, y, w, h, r);
            if (cov > 0) pblend(s, px, py, c, (int)(a * cov));
        }
    }
}
void rrect(Surf *s, float x, float y, float w, float h, float r, uint32_t c, int a) { rrect_vgrad(s, x, y, w, h, r, c, c, a); }
void rrect_line(Surf *s, float x, float y, float w, float h, float r, float t, uint32_t c, int a) {
    int x0 = MAX((int)floorf(x), s->cx0), y0 = MAX((int)floorf(y), s->cy0);
    int x1 = MIN((int)ceilf(x + w), s->cx1), y1 = MIN((int)ceilf(y + h), s->cy1);
    for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) {
        float fx = px + 0.5f, fy = py + 0.5f;
        float cov = rr_cov(fx, fy, x, y, w, h, r) - rr_cov(fx, fy, x + t, y + t, w - 2 * t, h - 2 * t, MAX(0, r - t));
        if (cov > 0) pblend(s, px, py, c, (int)(a * cov));
    }
}
void ellipse_soft(Surf *s, float cx, float cy, float rx, float ry, uint32_t c, int a) {
    if (rx <= 0 || ry <= 0) return;
    int x0 = MAX((int)(cx - rx), s->cx0), x1 = MIN((int)(cx + rx) + 1, s->cx1);
    int y0 = MAX((int)(cy - ry), s->cy0), y1 = MIN((int)(cy + ry) + 1, s->cy1);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, d = dx * dx + dy * dy;
        if (d >= 1) continue;
        float f = 1 - d; f *= f;
        pblend(s, x, y, c, (int)(a * f));
    }
}
void disc(Surf *s, float cx, float cy, float r, uint32_t c, int a) {
    int x0 = MAX((int)(cx - r - 1), s->cx0), x1 = MIN((int)(cx + r) + 2, s->cx1);
    int y0 = MAX((int)(cy - r - 1), s->cy0), y1 = MIN((int)(cy + r) + 2, s->cy1);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy, cov = clampf(r + 0.5f - sqrtf(dx * dx + dy * dy), 0, 1);
        if (cov > 0) pblend(s, x, y, c, (int)(a * cov));
    }
}
/* a triangle with 4x4 supersampled edges (small shapes: arrows, glyph-like icons) */
void tri_fill(Surf *s, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c, int a) {
    int bx0 = MAX((int)floorf(MIN(x0, MIN(x1, x2))), s->cx0), bx1 = MIN((int)ceilf(MAX(x0, MAX(x1, x2))), s->cx1);
    int by0 = MAX((int)floorf(MIN(y0, MIN(y1, y2))), s->cy0), by1 = MIN((int)ceilf(MAX(y0, MAX(y1, y2))), s->cy1);
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(area) < 1e-6f) return;
    float sg = area > 0 ? 1.0f : -1.0f;
    for (int y = by0; y < by1; y++) for (int x = bx0; x < bx1; x++) {
        int n = 0;
        for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) {
            float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
            float e0 = ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) * sg;
            float e1 = ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) * sg;
            float e2 = ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)) * sg;
            if (e0 >= 0 && e1 >= 0 && e2 >= 0) n++;
        }
        if (n) pblend(s, x, y, c, a * n / 16);
    }
}

void blit_region(Surf *s, const Img *im, int sx, int sy, int sw, int sh, int dx, int dy, int alpha) {
    if (!im->px || alpha <= 0) return;
    for (int y = 0; y < sh; y++) {
        int py = dy + y; if (py < s->cy0 || py >= s->cy1) continue;
        const uint32_t *src = im->px + (size_t)(sy + y) * im->w + sx;
        for (int x = 0; x < sw; x++) {
            uint32_t c = src[x]; int a = (int)CA(c);
            if (a) pblend(s, dx + x, py, c, alpha >= 255 ? a : a * alpha / 255);
        }
    }
}
void blit(Surf *s, const Img *im, int x, int y, int alpha) { blit_region(s, im, 0, 0, im->w, im->h, x, y, alpha); }

/* bilinear sample of a straight-alpha image, premultiplied while it is mixed (no dark fringes at the edges) */
static inline uint32_t sample_bilinear(const Img *im, float u, float v) {
    int x0 = (int)floorf(u), y0 = (int)floorf(v);
    float fx = u - x0, fy = v - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    x0 = CLAMP(x0, 0, im->w - 1); x1 = CLAMP(x1, 0, im->w - 1); y0 = CLAMP(y0, 0, im->h - 1); y1 = CLAMP(y1, 0, im->h - 1);
    uint32_t c[4] = { im->px[(size_t)y0 * im->w + x0], im->px[(size_t)y0 * im->w + x1], im->px[(size_t)y1 * im->w + x0], im->px[(size_t)y1 * im->w + x1] };
    float w[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
    float a = 0, r = 0, g = 0, b = 0;
    for (int i = 0; i < 4; i++) {
        float ca = CA(c[i]) * w[i];
        a += ca; r += CR(c[i]) * ca; g += CG(c[i]) * ca; b += CB(c[i]) * ca;
    }
    if (a < 0.5f) return 0;
    return ARGB((int)(a + 0.5f), (int)(r / a + 0.5f), (int)(g / a + 0.5f), (int)(b / a + 0.5f));
}

void blit_scaled(Surf *s, const Img *im, float dx, float dy, float dw, float dh, int alpha, int smooth) {
    if (!im->px || dw < 0.5f || dh < 0.5f || alpha <= 0) return;
    if (dw == im->w && dh == im->h && dx == floorf(dx) && dy == floorf(dy)) { blit(s, im, (int)dx, (int)dy, alpha); return; }
    int x0 = MAX((int)floorf(dx), s->cx0), x1 = MIN((int)ceilf(dx + dw), s->cx1);
    int y0 = MAX((int)floorf(dy), s->cy0), y1 = MIN((int)ceilf(dy + dh), s->cy1);
    float kx = im->w / dw, ky = im->h / dh;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float u = (x + 0.5f - dx) * kx, v = (y + 0.5f - dy) * ky;
        if (u < 0 || v < 0 || u >= im->w || v >= im->h) continue;
        uint32_t c = smooth ? sample_bilinear(im, u - 0.5f, v - 0.5f) : im->px[(size_t)(int)v * im->w + (int)u];
        int a = (int)CA(c);
        if (a) pblend(s, x, y, c, a * alpha / 255);
    }
}
void blit_tint(Surf *s, const Img *im, float dx, float dy, float dw, float dh, uint32_t col, int alpha) {
    if (!im->px || dw < 0.5f || dh < 0.5f || alpha <= 0) return;
    if (dw == im->w && dh == im->h && dx == floorf(dx) && dy == floorf(dy)) {     /* 1:1 at a whole pixel */
        int ox = (int)dx, oy = (int)dy;
        for (int y = MAX(0, s->cy0 - oy); y < MIN(im->h, s->cy1 - oy); y++) {
            const uint32_t *r = im->px + (size_t)y * im->w;
            for (int x = MAX(0, s->cx0 - ox); x < MIN(im->w, s->cx1 - ox); x++) { int a = (int)CA(r[x]); if (a) pblend(s, ox + x, oy + y, col, a * alpha / 255); }
        }
        return;
    }
    int x0 = MAX((int)floorf(dx), s->cx0), x1 = MIN((int)ceilf(dx + dw), s->cx1);
    int y0 = MAX((int)floorf(dy), s->cy0), y1 = MIN((int)ceilf(dy + dh), s->cy1);
    float kx = im->w / dw, ky = im->h / dh;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float u = (x + 0.5f - dx) * kx, v = (y + 0.5f - dy) * ky;
        if (u < 0 || v < 0 || u >= im->w || v >= im->h) continue;
        int a = (int)CA(sample_bilinear(im, u - 0.5f, v - 0.5f));
        if (a) pblend(s, x, y, col, a * alpha / 255);
    }
}

void img_fill(Img *im, uint32_t argb) { for (int i = 0; i < im->w * im->h; i++) im->px[i] = argb; }
void img_draw(Img *dst, const Img *src, int x, int y) {
    for (int sy = 0; sy < src->h; sy++) {
        int dy = y + sy; if (dy < 0 || dy >= dst->h) continue;
        for (int sx = 0; sx < src->w; sx++) {
            int dx = x + sx; if (dx < 0 || dx >= dst->w) continue;
            uint32_t s = src->px[(size_t)sy * src->w + sx], *d = &dst->px[(size_t)dy * dst->w + dx];
            int sa = (int)CA(s), da = (int)CA(*d);
            if (!sa) continue;
            if (sa == 255 || !da) { *d = s; continue; }
            int oa = sa + da * (255 - sa) / 255;
            uint32_t mixed = col_mix(*d | 0xff000000u, s | 0xff000000u, sa * 256 / 255);
            *d = (mixed & 0xffffff) | ((uint32_t)oa << 24);
        }
    }
}

/* an area-averaging resample of the source rectangle (sx, sy, sw, sh) to w x h: every source pixel counts by how
 * much of it each destination pixel covers. Premultiplied while summed. */
static void resample(const Img *src, float sx, float sy, float sw, float sh, Img *dst, int w, int h) {
    if (img_alloc(dst, w, h)) return;
    float kx = sw / w, ky = sh / h;
    int up = kx < 1 || ky < 1;           /* enlarging: bilinear is the right tool */
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        if (up) { dst->px[(size_t)y * w + x] = sample_bilinear(src, sx + (x + 0.5f) * kx - 0.5f, sy + (y + 0.5f) * ky - 0.5f); continue; }
        float fx0 = sx + x * kx, fx1 = fx0 + kx, fy0 = sy + y * ky, fy1 = fy0 + ky;
        float a = 0, r = 0, g = 0, b = 0, tw = 0;
        for (int yy = (int)floorf(fy0); yy < (int)ceilf(fy1); yy++) {
            float wy = MIN(fy1, yy + 1.0f) - MAX(fy0, (float)yy);
            if (wy <= 0) continue;
            int cy = CLAMP(yy, 0, src->h - 1);
            for (int xx = (int)floorf(fx0); xx < (int)ceilf(fx1); xx++) {
                float wx = MIN(fx1, xx + 1.0f) - MAX(fx0, (float)xx);
                if (wx <= 0) continue;
                uint32_t c = src->px[(size_t)cy * src->w + CLAMP(xx, 0, src->w - 1)];
                float wgt = wx * wy, ca = CA(c) * wgt;
                a += ca; r += CR(c) * ca; g += CG(c) * ca; b += CB(c) * ca; tw += wgt;
            }
        }
        dst->px[(size_t)y * w + x] = a < 0.5f ? 0 : ARGB((int)(a / tw + 0.5f), (int)(r / a + 0.5f), (int)(g / a + 0.5f), (int)(b / a + 0.5f));
    }
}
void img_rotate(const Img *src, float angle, Img *dst) {
    float c = cosf(angle), s = sinf(angle);
    int w = (int)ceilf(fabsf(src->w * c) + fabsf(src->h * s)) + 2, h = (int)ceilf(fabsf(src->w * s) + fabsf(src->h * c)) + 2;
    if (img_alloc(dst, w, h)) return;
    float cx = w / 2.0f, cy = h / 2.0f, sx = src->w / 2.0f, sy = src->h / 2.0f;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        /* 2x2 samples a pixel: the edges come out smooth */
        int a = 0, r = 0, g = 0, b = 0;
        for (int k = 0; k < 4; k++) {
            float px = x + 0.25f + (k & 1) * 0.5f - cx, py = y + 0.25f + (k >> 1) * 0.5f - cy;
            float u = px * c + py * s + sx, v = -px * s + py * c + sy;
            if (u < 0 || v < 0 || u >= src->w || v >= src->h) continue;
            uint32_t col = sample_bilinear(src, u - 0.5f, v - 0.5f);
            int ca = (int)CA(col);
            a += ca; r += (int)CR(col) * ca; g += (int)CG(col) * ca; b += (int)CB(col) * ca;
        }
        if (a) dst->px[(size_t)y * w + x] = ARGB(a / 4, r / a, g / a, b / a);
    }
}

void img_resize_area(const Img *src, Img *dst, int w, int h) { resample(src, 0, 0, (float)src->w, (float)src->h, dst, w, h); }
void img_resize_cover(const Img *src, Img *dst, int w, int h, float focus_y) {
    float k = MAX((float)w / src->w, (float)h / src->h);
    float sw = w / k, sh = h / k;
    float sx = (src->w - sw) * 0.5f, sy = (src->h - sh) * clampf(focus_y, 0, 1);
    resample(src, sx, sy, sw, sh, dst, w, h);
}
