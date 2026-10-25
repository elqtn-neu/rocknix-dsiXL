// r3d.c: see r3d.h. The scratch buffer holds premultiplied ARGB at twice the screen's resolution in each direction;
// r3d_end averages each 2x2 block onto the screen.
#include "r3d.h"
#include <stdlib.h>

static int SS = 2;                      /* samples a pixel, each way (r3d_quality); 0: one sample every 2x2 pixels */
static float SC = 2;                    /* the buffer's pixels per screen pixel, each way */
static int BW, BH;                      /* the buffer's size */
void r3d_quality(int ss) { SS = ss < 0 ? 0 : ss > 2 ? 2 : ss; SC = SS ? (float)SS : 0.5f; }

static float cam_cx, cam_cy, cam_f;
static Surf *target;
static int bx0, by0, bw, bh;            /* the box, in 1x pixels */
static uint32_t *buf; static size_t buf_cap;

M3 m3_rot(float yaw, float pitch, float roll) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    /* R = Rz(roll) * Rx(pitch) * Ry(yaw) */
    M3 ry = { { { cy, 0, sy }, { 0, 1, 0 }, { -sy, 0, cy } } };
    M3 rx = { { { 1, 0, 0 }, { 0, cp, -sp }, { 0, sp, cp } } };
    M3 rz = { { { cr, -sr, 0 }, { sr, cr, 0 }, { 0, 0, 1 } } };
    M3 a, b;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { a.m[i][j] = 0; for (int k = 0; k < 3; k++) a.m[i][j] += rx.m[i][k] * ry.m[k][j]; }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { b.m[i][j] = 0; for (int k = 0; k < 3; k++) b.m[i][j] += rz.m[i][k] * a.m[k][j]; }
    return b;
}

void r3d_camera(float cx, float cy, float f) { cam_cx = cx; cam_cy = cy; cam_f = f; }
void r3d_project(V3 p, float *sx, float *sy) {
    float z = MAX(p.z, 1.0f);
    *sx = cam_cx + cam_f * p.x / z; *sy = cam_cy - cam_f * p.y / z;
}

int r3d_begin(Surf *dst, int x0, int y0, int x1, int y1) {
    if (dst) { x0 = MAX(x0, dst->cx0); y0 = MAX(y0, dst->cy0); x1 = MIN(x1, dst->cx1); y1 = MIN(y1, dst->cy1); }
    target = dst; bx0 = x0; by0 = y0; bw = x1 - x0; bh = y1 - y0;
    if (bw <= 0 || bh <= 0) { bw = bh = 0; return -1; }
    if (!SS) { bw += bw & 1; bh += bh & 1; BW = bw / 2; BH = bh / 2; } else { BW = bw * SS; BH = bh * SS; }
    size_t need = (size_t)BW * BH;
    if (need > buf_cap) { free(buf); buf = malloc(need * 4); buf_cap = buf ? need : 0; if (!buf) { bw = bh = 0; return -1; } }
    memset(buf, 0, need * 4);
    return 0;
}

static inline uint32_t tex_sample(const Img *t, float u, float v) {
    float x = u * t->w - 0.5f, y = v * t->h - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    int fx = (int)((x - x0) * 256), fy = (int)((y - y0) * 256);
    int x1 = x0 + 1 < t->w ? x0 + 1 : t->w - 1, y1 = y0 + 1 < t->h ? y0 + 1 : t->h - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    const uint32_t *r0 = t->px + (size_t)y0 * t->w, *r1 = t->px + (size_t)y1 * t->w;
    uint32_t c00 = r0[x0], c10 = r0[x1], c01 = r1[x0], c11 = r1[x1];
    int w00 = (256 - fx) * (256 - fy), w10 = fx * (256 - fy), w01 = (256 - fx) * fy, w11 = fx * fy;   /* sum 65536 */
    /* two channels at a time (each 8 bits in a 16-bit lane: no overflow with weights up to 65536 >> 8) */
    uint32_t rb = (((c00 & 0xff00ff) * (uint32_t)(w00 >> 8) + (c10 & 0xff00ff) * (uint32_t)(w10 >> 8) +
                    (c01 & 0xff00ff) * (uint32_t)(w01 >> 8) + (c11 & 0xff00ff) * (uint32_t)(w11 >> 8)) >> 8) & 0xff00ff;
    uint32_t ag = ((((c00 >> 8) & 0xff00ff) * (uint32_t)(w00 >> 8) + ((c10 >> 8) & 0xff00ff) * (uint32_t)(w10 >> 8) +
                    ((c01 >> 8) & 0xff00ff) * (uint32_t)(w01 >> 8) + ((c11 >> 8) & 0xff00ff) * (uint32_t)(w11 >> 8)) >> 8) & 0xff00ff;
    return rb | (ag << 8);
}

typedef struct { float x, y, iw, uw, vw; } SV;     /* screen x, y (2x), 1/w, u/w, v/w */

static void raster_tri(const SV *a, const SV *b, const SV *c, const Mat *m) {
    float area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    if (fabsf(area) < 1e-4f) return;
    int W = BW, H = BH;
    int x0 = MAX(0, (int)floorf(MIN(a->x, MIN(b->x, c->x)))), x1 = MIN(W - 1, (int)ceilf(MAX(a->x, MAX(b->x, c->x))));
    int y0 = MAX(0, (int)floorf(MIN(a->y, MIN(b->y, c->y)))), y1 = MIN(H - 1, (int)ceilf(MAX(a->y, MAX(b->y, c->y))));
    if (x0 > x1 || y0 > y1) return;
    float inv = 1.0f / area;
    /* the barycentric weights are linear in x and y: stepped, not recomputed; each row walks only its span */
    float A0 = (b->y - c->y) * inv, B0 = (c->x - b->x) * inv, A1 = (c->y - a->y) * inv, B1 = (a->x - c->x) * inv;
    float A2 = -A0 - A1;
    float px0 = x0 + 0.5f, py0 = y0 + 0.5f;
    float W0 = ((b->x - px0) * (c->y - py0) - (c->x - px0) * (b->y - py0)) * inv;
    float W1 = ((c->x - px0) * (a->y - py0) - (a->x - px0) * (c->y - py0)) * inv;
    float diw = A0 * a->iw + A1 * b->iw + A2 * c->iw, duw = A0 * a->uw + A1 * b->uw + A2 * c->uw, dvw = A0 * a->vw + A1 * b->vw + A2 * c->vw;
    float shade = m->shade, spec = m->spec * 255;
    int ds = (int)(m->desat * 256), need_uv = m->tex || m->gloss > 0 || m->sheen > 0;
    int sh8 = (int)(shade * 256);
    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - y0);
        float e0 = W0 + B0 * dy, e1 = W1 + B1 * dy, e2 = 1 - e0 - e1;
        /* the pixels k (from x0) where every weight is >= 0 */
        float kmin = 0, kmax = (float)(x1 - x0);
        const float E[3] = { e0, e1, e2 }, A[3] = { A0, A1, A2 };
        int empty = 0;
        for (int i = 0; i < 3; i++) {
            if (A[i] > 1e-9f) kmin = MAX(kmin, -E[i] / A[i]);
            else if (A[i] < -1e-9f) kmax = MIN(kmax, E[i] / -A[i]);
            else if (E[i] < 0) empty = 1;
        }
        if (empty) continue;
        int k0 = (int)ceilf(kmin - 1e-4f), k1 = (int)floorf(kmax + 1e-4f);
        if (k0 > k1) continue;
        float w0 = e0 + A0 * k0, w1 = e1 + A1 * k0, w2 = 1 - w0 - w1;
        float iw = w0 * a->iw + w1 * b->iw + w2 * c->iw, uw = w0 * a->uw + w1 * b->uw + w2 * c->uw, vw = w0 * a->vw + w1 * b->vw + w2 * c->vw;
        uint32_t *dp = &buf[(size_t)y * W + x0 + k0];
        for (int k = k0; k <= k1; k++, dp++, iw += diw, uw += duw, vw += dvw) {
            uint32_t col;
            float u = 0, v = 0;
            if (need_uv) { float r = 1.0f / iw; u = uw * r; v = vw * r; }
            if (m->tex) {
                const Img *t = m->tex;
                float uc = u < 0 ? 0 : u > 0.9999f ? 0.9999f : u, vc = v < 0 ? 0 : v > 0.9999f ? 0.9999f : v;
                if (m->nearest) col = t->px[(size_t)(int)(vc * t->h) * t->w + (int)(uc * t->w)];
                else col = tex_sample(t, uc, vc);
                if (CA(col) < 8) continue;
            } else col = m->color | 0xff000000u;
            if (ds) col = col_desat(col, ds);
            float add = spec;
            if (m->sheen > 0) add += m->sheen * 70 * (1 - v) * (1 - v);
            if (m->gloss > 0) {
                float gd = (u + v * 0.55f) - m->gloss_pos;
                if (gd > -0.45f && gd < 0.35f) {
                    /* a soft diagonal band of light sweeping across the face */
                    float band = expf(-gd * gd * 90.0f) * 0.9f + expf(-(gd + 0.09f) * (gd + 0.09f) * 900.0f) * 0.5f;
                    add += band * m->gloss * 200;
                }
            }
            int ad = (int)add;
            int r = ((int)CR(col) * sh8 >> 8) + ad, g = ((int)CG(col) * sh8 >> 8) + ad, bl = ((int)CB(col) * sh8 >> 8) + ad;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (bl > 255) bl = 255;
            int a8 = (int)CA(col);
            if (a8 >= 250) { *dp = ARGB(255, r, g, bl); continue; }
            /* premultiplied over */
            r = r * a8 / 255; g = g * a8 / 255; bl = bl * a8 / 255;
            uint32_t d = *dp; int ia = 255 - a8;
            *dp = ARGB(MIN(255, a8 + (int)CA(d) * ia / 255), r + (int)CR(d) * ia / 255, g + (int)CG(d) * ia / 255, bl + (int)CB(d) * ia / 255);
        }
    }
}

void r3d_poly(const V3 *v, const float *uv, int n, const Mat *m) {
    if (!bw || n < 3 || n > 16) return;
    SV s[16];
    for (int i = 0; i < n; i++) {
        float z = MAX(v[i].z, 1.0f), iw = 1.0f / z;
        s[i].x = (cam_cx + cam_f * v[i].x * iw - bx0) * SC;
        s[i].y = (cam_cy - cam_f * v[i].y * iw - by0) * SC;
        s[i].iw = iw; s[i].uw = uv[i * 2] * iw; s[i].vw = uv[i * 2 + 1] * iw;
    }
    for (int i = 1; i + 1 < n; i++) raster_tri(&s[0], &s[i], &s[i + 1], m);
}

/* one pixel's samples averaged: premultiplied in, straight colour and coverage (0..255) out */
static inline uint32_t resolve(int x, int y) {
    int W = BW;
    if (SS <= 1) {
        uint32_t c = SS ? buf[(size_t)y * W + x] : buf[(size_t)(y >> 1) * W + (x >> 1)]; int a = (int)CA(c);
        return a ? ARGB(a, CR(c) * 255 / a, CG(c) * 255 / a, CB(c) * 255 / a) : 0;
    }
    const uint32_t *p = &buf[(size_t)(y * SS) * W + x * SS];
    uint32_t c[4] = { p[0], p[1], p[W], p[W + 1] };
    int a = 0, r = 0, g = 0, b = 0;
    for (int i = 0; i < 4; i++) { a += (int)CA(c[i]); r += (int)CR(c[i]); g += (int)CG(c[i]); b += (int)CB(c[i]); }
    return a ? ARGB(a / 4, r * 255 / a, g * 255 / a, b * 255 / a) : 0;
}

int r3d_end_img(Img *out, int *ox, int *oy) {
    if (!bw) return -1;
    if (img_alloc(out, bw, bh)) { bw = bh = 0; return -1; }
    for (int y = 0; y < bh; y++) for (int x = 0; x < bw; x++) out->px[(size_t)y * bw + x] = resolve(x, y);
    *ox = bx0; *oy = by0;
    bw = bh = 0;
    return 0;
}

void r3d_end(int alpha) {
    if (!bw || alpha <= 0) return;
    for (int y = 0; y < bh; y++) for (int x = 0; x < bw; x++) {
        uint32_t c = resolve(x, y); int a = (int)CA(c);
        if (a) pblend(target, bx0 + x, by0 + y, c, a * alpha / 255);
    }
    bw = bh = 0;
}
