// hgfx.c: see hgfx.h.
#include "hgfx.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static inline void blend_px(uint32_t *p, uint32_t c, int a) {
    if (a >= 255) { *p = c & 0xffffff; return; }
    uint32_t d = *p, k = (uint32_t)a + ((uint32_t)a >> 7), ik = 256 - k;
    uint32_t rb = (((c & 0xff00ff) * k + (d & 0xff00ff) * ik) >> 8) & 0xff00ff;
    uint32_t g = (((c & 0x00ff00) * k + (d & 0x00ff00) * ik) >> 8) & 0x00ff00;
    *p = rb | g;
}
static inline void span(Surf *s, int y, int x0, int x1, uint32_t c, int a) {
    if (y < s->cy0 || y >= s->cy1 || a <= 0) return;
    if (x0 < s->cx0) x0 = s->cx0;
    if (x1 > s->cx1) x1 = s->cx1;
    if (x1 <= x0) return;
    uint32_t *p = s->px + (size_t)y * s->pitch;
    if (a >= 255) { uint32_t v = c & 0xffffff; for (int x = x0; x < x1; x++) p[x] = v; return; }
    for (int x = x0; x < x1; x++) blend_px(p + x, c, a);
}
static inline void px_cov(Surf *s, int x, int y, uint32_t c, int a, float cov) {
    if (cov <= 0 || x < s->cx0 || x >= s->cx1 || y < s->cy0 || y >= s->cy1) return;
    int k = (int)(a * (cov > 1 ? 1 : cov) + 0.5f);
    if (k > 0) blend_px(s->px + (size_t)y * s->pitch + x, c, k);
}

uint32_t h_mix(uint32_t a, uint32_t b, float t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    int ar = (int)CR(a), ag = (int)CG(a), ab = (int)CB(a);
    int r = (int)(ar + ((int)CR(b) - ar) * t + .5f), g = (int)(ag + ((int)CG(b) - ag) * t + .5f), bl = (int)(ab + ((int)CB(b) - ab) * t + .5f);
    return RGB(r, g, bl);
}

/* one rounded rectangle's row: solid middle, corners by coverage; color per row */
static void rr_row(Surf *s, int x, int w, int r, int j, int h, int yy, uint32_t c, int a) {
    if (yy < s->cy0 || yy >= s->cy1) return;
    if (j >= r && j < h - r) { span(s, yy, x, x + w, c, a); return; }
    float dy = j < r ? r - j - 0.5f : j - (h - r) + 0.5f;
    for (int i = 0; i < r; i++) {
        float dx = r - i - 0.5f, cov = r - sqrtf(dx * dx + dy * dy) + 0.5f;
        if (cov <= 0) continue;
        px_cov(s, x + i, yy, c, a, cov);
        px_cov(s, x + w - 1 - i, yy, c, a, cov);
    }
    span(s, yy, x + r, x + w - r, c, a);
}
void hr_fill(Surf *s, int x, int y, int w, int h, int r, uint32_t c, int a) {
    if (w <= 0 || h <= 0 || a <= 0) return;
    r = MAX(0, MIN(r, MIN(w, h) / 2));
    int j0 = MAX(0, s->cy0 - y), j1 = MIN(h, s->cy1 - y);
    for (int j = j0; j < j1; j++) rr_row(s, x, w, r, j, h, y + j, c, a);
}
void hr_vgrad(Surf *s, int x, int y, int w, int h, int r, uint32_t top, uint32_t bot, int a) {
    if (w <= 0 || h <= 0 || a <= 0) return;
    r = MAX(0, MIN(r, MIN(w, h) / 2));
    int j0 = MAX(0, s->cy0 - y), j1 = MIN(h, s->cy1 - y);
    for (int j = j0; j < j1; j++) rr_row(s, x, w, r, j, h, y + j, h_mix(top, bot, h > 1 ? (float)j / (h - 1) : 0), a);
}
/* signed distance to a rounded rectangle (centre cx, cy; half size hx, hy; radius r) */
static inline float rr_sd(float px, float py, float cx, float cy, float hx, float hy, float r) {
    float qx = fabsf(px - cx) - (hx - r), qy = fabsf(py - cy) - (hy - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy; if (in > 0) in = 0;
    return sqrtf(ox * ox + oy * oy) + in - r;
}
void hr_ring(Surf *s, int x, int y, int w, int h, int r, int t, uint32_t c, int a) {
    if (w <= 0 || h <= 0 || a <= 0 || t <= 0) return;
    r = MAX(0, MIN(r, MIN(w, h) / 2));
    float cx = x + w / 2.0f, cy = y + h / 2.0f, hx = w / 2.0f, hy = h / 2.0f;
    int band = t + r + 1;
    for (int j = 0; j < h; j++) {
        int yy = y + j; if (yy < s->cy0 || yy >= s->cy1) continue;
        int mid = j >= band && j < h - band;
        for (int i = 0; i < w; i++) {
            if (mid && i >= band && i < w - band) { i = w - band - 1; continue; }
            float d = rr_sd(x + i + .5f, yy + .5f, cx, cy, hx, hy, (float)r);
            float cov = (.5f - d); cov = cov > 1 ? 1 : cov;
            float inner = .5f - (d + t); inner = inner < 0 ? 0 : inner > 1 ? 1 : inner;
            px_cov(s, x + i, yy, c, a, cov - inner);
        }
    }
}

/* ---- shadows: an alpha mask per size, blurred once, kept (the last 24) ---- */
typedef struct { int w, h, r, blur; uint8_t *m; unsigned use; } Shadow;
static Shadow sh[48]; static unsigned sh_clock;
static void box_blur(uint8_t *m, int W, int H, int rad) {
    if (rad < 1) return;
    uint8_t *t = malloc((size_t)MAX(W, H));
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < H; y++) {
            uint8_t *row = m + (size_t)y * W; int sum = 0, n = 2 * rad + 1;
            for (int i = -rad; i <= rad; i++) sum += row[CLAMP(i, 0, W - 1)];
            for (int x = 0; x < W; x++) {
                t[x] = (uint8_t)(sum / n);
                sum += row[CLAMP(x + rad + 1, 0, W - 1)] - row[CLAMP(x - rad, 0, W - 1)];
            }
            memcpy(row, t, (size_t)W);
        }
        for (int x = 0; x < W; x++) {
            int sum = 0, n = 2 * rad + 1;
            for (int i = -rad; i <= rad; i++) sum += m[(size_t)CLAMP(i, 0, H - 1) * W + x];
            for (int y = 0; y < H; y++) {
                t[y] = (uint8_t)(sum / n);
                sum += m[(size_t)CLAMP(y + rad + 1, 0, H - 1) * W + x] - m[(size_t)CLAMP(y - rad, 0, H - 1) * W + x];
            }
            for (int y = 0; y < H; y++) m[(size_t)y * W + x] = t[y];
        }
    }
    free(t);
}
static Shadow *shadow_get(int w, int h, int r, int blur) {
    for (int i = 0; i < 48; i++) if (sh[i].m && sh[i].w == w && sh[i].h == h && sh[i].r == r && sh[i].blur == blur) { sh[i].use = ++sh_clock; return &sh[i]; }
    int k = 0; for (int i = 1; i < 48; i++) if (!sh[i].m || (sh[k].m && sh[i].use < sh[k].use)) k = i;
    free(sh[k].m);
    int W = w + 2 * blur, H = h + 2 * blur;
    uint8_t *m = calloc((size_t)W * H, 1);
    if (!m) { sh[k].m = 0; return 0; }
    float cx = blur + w / 2.0f, cy = blur + h / 2.0f;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float d = rr_sd(x + .5f, y + .5f, cx, cy, w / 2.0f, h / 2.0f, (float)MIN(r, MIN(w, h) / 2));
        float cov = .5f - d; m[(size_t)y * W + x] = (uint8_t)(255 * (cov < 0 ? 0 : cov > 1 ? 1 : cov));
    }
    box_blur(m, W, H, MAX(1, blur / 3));
    sh[k] = (Shadow){ w, h, r, blur, m, ++sh_clock };
    return &sh[k];
}
/* A shadow of any size from one square tile per corner radius and blur, its middle row and column stretched (nine
 * slices): a card that grows as it's pressed, or a new size of window, costs no new blur. Under the card itself
 * (inset by its corners) nothing is drawn: the card covers it. */
void hr_shadow(Surf *s, int x, int y, int w, int h, int r, int blur, int dy, int a) {
    if (w <= 0 || h <= 0 || a <= 0 || blur < 0) return;
    r = MAX(0, MIN(r, MIN(w, h) / 2));
    int R0 = 2 * r + 4 * blur + 2;                  /* the tile's rectangle: big enough that its corners don't meet */
    Shadow *S; int T, half;
    int q = 0;                                      /* small: a mask of a size rounded up to 8, drawn stretched */
    if (w >= R0 && h >= R0) { S = shadow_get(R0, R0, r, blur); T = R0 + 2 * blur; half = T / 2; }
    else { S = shadow_get((w + 7) & ~7, (h + 7) & ~7, r, blur); T = 0; half = 0; q = 1; }
    if (!S) return;
    int TW = S->w + 2 * blur, TH = S->h + 2 * blur;
    int W = w + 2 * blur, H = h + 2 * blur, ox = x - blur, oy = y - blur + dy;
    int i0 = MAX(0, s->cx0 - ox), i1 = MIN(W, s->cx1 - ox);
    int ka = a + (a >> 7);
    for (int j = 0; j < H; j++) {
        int yy = oy + j; if (yy < s->cy0 || yy >= s->cy1) continue;
        int tj = q ? j * TH / H : j < half ? j : j >= H - half ? T - (H - j) : half;
        const uint8_t *m = S->m + (size_t)tj * TW;
        uint32_t *p = s->px + (size_t)yy * s->pitch + ox;
        /* (the part the card will cover) */
        int c0 = W, c1 = W;
        if (yy >= y + r && yy < y + h - r) { c0 = blur + r; c1 = blur + w - r; }
        for (int i = i0; i < i1; i++) {
            if (i == c0) { i = c1 - 1; continue; }
            int ti = q ? i * TW / W : i < half ? i : i >= W - half ? T - (W - i) : half;
            int v = m[ti]; if (!v) continue;
            int k = (v * ka) >> 8; if (k <= 0) continue;
            uint32_t d = p[i]; int ik = 256 - (k + (k >> 7));
            p[i] = ((((d & 0xff00ff) * (uint32_t)ik) >> 8) & 0xff00ff) | ((((d & 0x00ff00) * (uint32_t)ik) >> 8) & 0x00ff00);
        }
    }
}
void h_glow(Surf *s, float cx, float cy, float rad, uint32_t c, int a) {
    int x0 = MAX((int)(cx - rad), s->cx0), x1 = MIN((int)(cx + rad) + 1, s->cx1);
    int y0 = MAX((int)(cy - rad), s->cy0), y1 = MIN((int)(cy + rad) + 1, s->cy1);
    float ir = 1 / rad;
    for (int y = y0; y < y1; y++) {
        uint32_t *p = s->px + (size_t)y * s->pitch; float dy = (y + .5f - cy) * ir;
        for (int x = x0; x < x1; x++) {
            float dx = (x + .5f - cx) * ir, d = dx * dx + dy * dy;
            if (d >= 1) continue;
            float f = 1 - d; f *= f;
            int k = (int)(a * f); if (k > 0) blend_px(p + x, c, k);
        }
    }
}

/* ---- icons ---- */
static inline float seg_dist(float px, float py, float x0, float y0, float x1, float y1) {
    float vx = x1 - x0, vy = y1 - y0, wx = px - x0, wy = py - y0, l = vx * vx + vy * vy;
    float t = l > 0 ? (wx * vx + wy * vy) / l : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
    float dx = wx - vx * t, dy = wy - vy * t;
    return sqrtf(dx * dx + dy * dy);
}
void hi_line(Surf *s, float x0, float y0, float x1, float y1, float w, uint32_t c, int a) {
    float hw = w / 2;
    int bx0 = MAX((int)floorf(MIN(x0, x1) - hw - 1), s->cx0), bx1 = MIN((int)ceilf(MAX(x0, x1) + hw + 1), s->cx1);
    int by0 = MAX((int)floorf(MIN(y0, y1) - hw - 1), s->cy0), by1 = MIN((int)ceilf(MAX(y0, y1) + hw + 1), s->cy1);
    for (int y = by0; y < by1; y++) for (int x = bx0; x < bx1; x++)
        px_cov(s, x, y, c, a, hw + .5f - seg_dist(x + .5f, y + .5f, x0, y0, x1, y1));
}
void hi_circle(Surf *s, float cx, float cy, float r, float w, uint32_t c, int a) {
    float ext = r + (w > 0 ? w / 2 : 0) + 1;
    int x0 = MAX((int)(cx - ext), s->cx0), x1 = MIN((int)(cx + ext) + 1, s->cx1);
    int y0 = MAX((int)(cy - ext), s->cy0), y1 = MIN((int)(cy + ext) + 1, s->cy1);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float dx = x + .5f - cx, dy = y + .5f - cy, l = sqrtf(dx * dx + dy * dy);
        float d = w > 0 ? fabsf(l - r) - w / 2 : l - r;
        px_cov(s, x, y, c, a, .5f - d);
    }
}
void hi_arc(Surf *s, float cx, float cy, float r, float a0, float a1, float w, uint32_t c, int a) {
    float ext = r + w / 2 + 1;
    float ex0 = cx + cosf(a0) * r, ey0 = cy + sinf(a0) * r, ex1 = cx + cosf(a1) * r, ey1 = cy + sinf(a1) * r;
    int x0 = MAX((int)(cx - ext), s->cx0), x1 = MIN((int)(cx + ext) + 1, s->cx1);
    int y0 = MAX((int)(cy - ext), s->cy0), y1 = MIN((int)(cy + ext) + 1, s->cy1);
    float span_ = a1 - a0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float dx = x + .5f - cx, dy = y + .5f - cy, l = sqrtf(dx * dx + dy * dy);
        float an = atan2f(dy, dx) - a0;
        while (an < 0) an += 2 * PI_F;
        while (an >= 2 * PI_F) an -= 2 * PI_F;
        float d;
        if (an <= span_) d = fabsf(l - r) - w / 2;
        else {
            float d0 = sqrtf((x + .5f - ex0) * (x + .5f - ex0) + (y + .5f - ey0) * (y + .5f - ey0));
            float d1 = sqrtf((x + .5f - ex1) * (x + .5f - ex1) + (y + .5f - ey1) * (y + .5f - ey1));
            d = (d0 < d1 ? d0 : d1) - w / 2;
        }
        px_cov(s, x, y, c, a, .5f - d);
    }
}
void hi_poly(Surf *s, const float *xy, int n, uint32_t c, int a) {
    if (n < 3) return;
    float mnx = xy[0], mxx = xy[0], mny = xy[1], mxy = xy[1];
    for (int i = 1; i < n; i++) { mnx = MIN(mnx, xy[2 * i]); mxx = MAX(mxx, xy[2 * i]); mny = MIN(mny, xy[2 * i + 1]); mxy = MAX(mxy, xy[2 * i + 1]); }
    int x0 = MAX((int)floorf(mnx) - 1, s->cx0), x1 = MIN((int)ceilf(mxx) + 1, s->cx1);
    int y0 = MAX((int)floorf(mny) - 1, s->cy0), y1 = MIN((int)ceilf(mxy) + 1, s->cy1);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float px = x + .5f, py = y + .5f, dmin = 1e9f; int in = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
            if (((yi > py) != (yj > py)) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
            float d = seg_dist(px, py, xi, yi, xj, yj); if (d < dmin) dmin = d;
        }
        px_cov(s, x, y, c, a, in ? .5f + dmin : .5f - dmin);
    }
}

/* ---- sprites ---- */
void hb_blit(Surf *s, const Img *im, int x, int y, int a) {
    if (!im->px || a <= 0) return;
    int i0 = MAX(0, s->cx0 - x), i1 = MIN(im->w, s->cx1 - x), j0 = MAX(0, s->cy0 - y), j1 = MIN(im->h, s->cy1 - y);
    for (int j = j0; j < j1; j++) {
        const uint32_t *src = im->px + (size_t)j * im->w; uint32_t *d = s->px + (size_t)(y + j) * s->pitch + x;
        for (int i = i0; i < i1; i++) {
            uint32_t c = src[i]; int k = (int)(c >> 24); if (!k) continue;
            if (a < 255) k = k * a / 255;
            blend_px(d + i, c, k);
        }
    }
}
void hb_scaled(Surf *s, const Img *im, float x, float y, float w, float h, int a) {
    if (!im->px || a <= 0 || w < 1 || h < 1) return;
    int X0 = (int)floorf(x), Y0 = (int)floorf(y), X1 = (int)ceilf(x + w), Y1 = (int)ceilf(y + h);
    int x0 = MAX(X0, s->cx0), x1 = MIN(X1, s->cx1), y0 = MAX(Y0, s->cy0), y1 = MIN(Y1, s->cy1);
    uint32_t sx = (uint32_t)(im->w * 65536.0f / w), sy = (uint32_t)(im->h * 65536.0f / h);
    for (int yy = y0; yy < y1; yy++) {
        int v = (int)(((yy + .5f - y) * sy) / 65536.0f); if (v < 0 || v >= im->h) continue;
        const uint32_t *src = im->px + (size_t)v * im->w; uint32_t *d = s->px + (size_t)yy * s->pitch;
        /* (the first column's texel: a hair left of the image when x has a fraction over a half: clamped to it) */
        int64_t u0 = (int64_t)((x0 + .5f - x) * sx);
        uint32_t u = (uint32_t)(u0 < 0 ? 0 : u0);
        for (int xx = x0; xx < x1; xx++, u += sx) {
            int ui = (int)(u >> 16); if (ui >= im->w) break;
            uint32_t c = src[ui]; int k = (int)(c >> 24);
            if (!k) continue;
            if (a < 255) k = k * a / 255;
            blend_px(d + xx, c, k);
        }
    }
}
int hb_capture(Img *out, int w, int h, hb_draw_fn draw, void *ctx) {
    Surf b, wt; surf_alloc(&b, w, h); surf_alloc(&wt, w, h);
    if (!b.px || !wt.px) { surf_free(&b); surf_free(&wt); return -1; }
    fill(&b, 0x000000); fill(&wt, 0xffffff);
    draw(&b, ctx); draw(&wt, ctx);
    if (img_alloc(out, w, h)) { surf_free(&b); surf_free(&wt); return -1; }
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        uint32_t k = b.px[(size_t)y * b.pitch + x], q = wt.px[(size_t)y * wt.pitch + x];
        int al = 255 - ((int)CG(q) - (int)CG(k));
        al = al < 0 ? 0 : al > 255 ? 255 : al;
        uint32_t c = 0;
        if (al > 0) c = ARGB(al, MIN(255, CR(k) * 255 / al), MIN(255, CG(k) * 255 / al), MIN(255, CB(k) * 255 / al));
        out->px[(size_t)y * w + x] = c;
    }
    surf_free(&b); surf_free(&wt);
    return 0;
}
void hb_fit(const Img *src, Img *dst, int w, int h, int cover) {
    dst->px = 0; dst->w = dst->h = 0;
    if (!src->px || w <= 0 || h <= 0 || img_alloc(dst, w, h)) return;
    float sc = cover ? MAX((float)w / src->w, (float)h / src->h) : MIN((float)w / src->w, (float)h / src->h);
    float vw = w / sc, vh = h / sc, ox = (src->w - vw) / 2, oy = (src->h - vh) / 2;
    if (!cover) img_fill(dst, 0);
    /* each output pixel: the average of the source pixels under it (a box when shrinking, at least one pixel when
     * growing), in whole numbers; the spans worked out once per column and per row */
    int *xs = malloc(sizeof(int) * 2 * (size_t)(w + h));
    if (!xs) return;
    int *ys = xs + 2 * w;
    for (int x = 0; x < w; x++) {
        int i0 = (int)floorf(ox + x / sc), i1 = (int)ceilf(ox + (x + 1) / sc);
        i0 = CLAMP(i0, 0, src->w - 1); i1 = CLAMP(MAX(i1, i0 + 1), i0 + 1, src->w);
        xs[2 * x] = i0; xs[2 * x + 1] = i1;
    }
    for (int y = 0; y < h; y++) {
        int j0 = (int)floorf(oy + y / sc), j1 = (int)ceilf(oy + (y + 1) / sc);
        ys[2 * y] = -1;
        if (!cover && (oy + (y + 1) / sc <= 0 || oy + y / sc >= src->h)) continue;
        j0 = CLAMP(j0, 0, src->h - 1); j1 = CLAMP(MAX(j1, j0 + 1), j0 + 1, src->h);
        ys[2 * y] = j0; ys[2 * y + 1] = j1;
    }
    for (int y = 0; y < h; y++) {
        int j0 = ys[2 * y], j1 = ys[2 * y + 1];
        if (j0 < 0) continue;
        uint32_t *out = dst->px + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            if (!cover) { float fx = ox + (x + .5f) / sc; if (fx < 0 || fx >= src->w) continue; }
            int i0 = xs[2 * x], i1 = xs[2 * x + 1];
            uint32_t A = 0, R = 0, G = 0, B = 0, n = 0;
            for (int j = j0; j < j1; j++) {
                const uint32_t *row = src->px + (size_t)j * src->w;
                for (int i = i0; i < i1; i++) {
                    uint32_t c = row[i], a = c >> 24;
                    A += a; R += ((c >> 16) & 255) * a; G += ((c >> 8) & 255) * a; B += (c & 255) * a;
                }
                n += (uint32_t)(i1 - i0);
            }
            if (!A) continue;
            out[x] = ARGB((int)((A + n / 2) / n), (int)(R / A), (int)(G / A), (int)(B / A));
        }
    }
    free(xs);
}
void hb_stretch(Surf *s, const Img *im, int y0, int y1) {
    if (!im->px) return;
    y0 = MAX(y0, s->cy0); y1 = MIN(y1, s->cy1);
    int W = s->w;
    for (int y = y0; y < y1; y++) {
        int vy = (int)(((y + .5f) * im->h / s->h - .5f) * 256);
        int v0 = vy >> 8, fy = vy & 255; if (vy < 0) { v0 = 0; fy = 0; }
        int v1 = MIN(v0 + 1, im->h - 1); v0 = MIN(v0, im->h - 1);
        const uint32_t *r0 = im->px + (size_t)v0 * im->w, *r1 = im->px + (size_t)v1 * im->w;
        uint32_t *d = s->px + (size_t)y * s->pitch;
        for (int x = 0; x < W; x++) {
            int ux = (int)(((x + .5f) * im->w / W - .5f) * 256);
            int u0 = ux >> 8, fx = ux & 255; if (ux < 0) { u0 = 0; fx = 0; }
            int u1 = MIN(u0 + 1, im->w - 1); u0 = MIN(u0, im->w - 1);
            uint32_t a = r0[u0], b = r0[u1], c = r1[u0], e = r1[u1];
            int rr = ((CR(a) * (256 - fx) + CR(b) * fx) * (256 - fy) + (CR(c) * (256 - fx) + CR(e) * fx) * fy) >> 16;
            int gg = ((CG(a) * (256 - fx) + CG(b) * fx) * (256 - fy) + (CG(c) * (256 - fx) + CG(e) * fx) * fy) >> 16;
            int bb = ((CB(a) * (256 - fx) + CB(b) * fx) * (256 - fy) + (CB(c) * (256 - fx) + CB(e) * fx) * fy) >> 16;
            d[x] = RGB(rr, gg, bb);
        }
    }
}

void h_main_colors(const Img *im, uint32_t *vivid, uint32_t *deep) {
    *vivid = 0x4a7fd0; *deep = 0x1f3560;
    if (!im->px || im->w < 1) return;
    /* hue bins weighted by how vivid each pixel is; the strongest bin's average is the vivid colour */
    float wsum[12] = { 0 }, rs[12] = { 0 }, gs[12] = { 0 }, bs[12] = { 0 }, dr = 0, dg = 0, db = 0, dw = 0;
    int step = MAX(1, MIN(im->w, im->h) / 24);
    for (int y = 0; y < im->h; y += step) for (int x = 0; x < im->w; x += step) {
        uint32_t c = im->px[(size_t)y * im->w + x]; if (CA(c) < 128) continue;
        float r = CR(c) / 255.f, g = CG(c) / 255.f, b = CB(c) / 255.f;
        float mx = MAX(r, MAX(g, b)), mn = MIN(r, MIN(g, b)), sat = mx > 0 ? (mx - mn) / mx : 0;
        float hue = 0;
        if (mx > mn) {
            if (mx == r) hue = fmodf((g - b) / (mx - mn) + 6, 6);
            else if (mx == g) hue = (b - r) / (mx - mn) + 2;
            else hue = (r - g) / (mx - mn) + 4;
        }
        int bin = (int)(hue * 2) % 12;
        float w = sat * sat * mx * (mx < 0.95f || sat > 0.2f ? 1 : 0.2f);
        wsum[bin] += w; rs[bin] += r * w; gs[bin] += g * w; bs[bin] += b * w;
        float dk = (1 - mx) * (1 - mx);
        dr += r * dk; dg += g * dk; db += b * dk; dw += dk;
    }
    int best = 0; for (int i = 1; i < 12; i++) if (wsum[i] > wsum[best]) best = i;
    if (wsum[best] > 0.01f) {
        float r = rs[best] / wsum[best], g = gs[best] / wsum[best], b = bs[best] / wsum[best];
        /* a little more colour and light, so the backdrop sings */
        float mx = MAX(r, MAX(g, b)); if (mx < 0.75f && mx > 0) { r *= 0.75f / mx; g *= 0.75f / mx; b *= 0.75f / mx; }
        *vivid = RGB((int)(r * 255), (int)(g * 255), (int)(b * 255));
    }
    *deep = h_mix(*vivid, dw > 0.01f ? RGB((int)(dr / dw * 255), (int)(dg / dw * 255), (int)(db / dw * 255)) : 0, 0.55f);
    *deep = h_mix(*deep, 0x000000, 0.35f);
}
