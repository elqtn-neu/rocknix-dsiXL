// r3d.c: see r3d.h. The scratch buffer holds premultiplied ARGB at twice the screen's resolution in each direction;
// r3d_end averages each 2x2 block onto the screen.
#include "r3d.h"
#include <stdlib.h>

#define SS 2

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
    size_t need = (size_t)bw * bh * SS * SS;
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
    int W = bw * SS, H = bh * SS;
    int x0 = MAX(0, (int)floorf(MIN(a->x, MIN(b->x, c->x)))), x1 = MIN(W - 1, (int)ceilf(MAX(a->x, MAX(b->x, c->x))));
    int y0 = MAX(0, (int)floorf(MIN(a->y, MIN(b->y, c->y)))), y1 = MIN(H - 1, (int)ceilf(MAX(a->y, MAX(b->y, c->y))));
    if (x0 > x1 || y0 > y1) return;
    float inv = 1.0f / area;
    float shade = m->shade, spec = m->spec * 255, desat = m->desat;
    int ds = (int)(desat * 256);
    for (int y = y0; y <= y1; y++) {
        float py = y + 0.5f;
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f;
            float w0 = ((b->x - px) * (c->y - py) - (c->x - px) * (b->y - py)) * inv;
            float w1 = ((c->x - px) * (a->y - py) - (a->x - px) * (c->y - py)) * inv;
            float w2 = 1 - w0 - w1;
            /* a shared edge belongs to one side only (no seams, no double blending) */
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            uint32_t col;
            float u = 0, v = 0;
            if (m->tex || m->gloss > 0 || m->sheen > 0) {
                float iw = w0 * a->iw + w1 * b->iw + w2 * c->iw;
                u = (w0 * a->uw + w1 * b->uw + w2 * c->uw) / iw;
                v = (w0 * a->vw + w1 * b->vw + w2 * c->vw) / iw;
            }
            if (m->tex) {
                if (m->nearest) {
                    const Img *t = m->tex;
                    int tx = (int)(clampf(u, 0, 0.9999f) * t->w), ty = (int)(clampf(v, 0, 0.9999f) * t->h);
                    col = t->px[(size_t)ty * t->w + tx];
                } else col = tex_sample(m->tex, clampf(u, 0, 0.9999f), clampf(v, 0, 0.9999f));
                if (CA(col) < 8) continue;
            } else col = m->color | 0xff000000u;
            if (ds) col = col_desat(col, ds);
            float r = CR(col) * shade, g = CG(col) * shade, bl = CB(col) * shade, add = spec;
            if (m->sheen > 0) add += m->sheen * 70 * (1 - v) * (1 - v);
            float gd = (u + v * 0.55f) - m->gloss_pos;
            if (m->gloss > 0 && gd > -0.45f && gd < 0.35f) {
                /* a soft diagonal band of light sweeping across the face */
                float d = gd;
                float band = expf(-d * d * 90.0f) * 0.9f + expf(-(d + 0.09f) * (d + 0.09f) * 900.0f) * 0.5f;
                add += band * m->gloss * 200;
            }
            r += add; g += add; bl += add;
            int a8 = (int)CA(col);
            uint32_t out = ARGB(255, (int)MIN(255, r) * a8 / 255, (int)MIN(255, g) * a8 / 255, (int)MIN(255, bl) * a8 / 255);
            uint32_t *dp = &buf[(size_t)y * W + x];
            if (a8 >= 250) *dp = out;
            else {
                /* premultiplied over */
                uint32_t d = *dp; int ia = 255 - a8;
                *dp = ARGB(MIN(255, a8 + (int)CA(d) * ia / 255), CR(out) + (int)CR(d) * ia / 255, CG(out) + (int)CG(d) * ia / 255, CB(out) + (int)CB(d) * ia / 255);
            }
        }
    }
}

void r3d_poly(const V3 *v, const float *uv, int n, const Mat *m) {
    if (!bw || n < 3 || n > 16) return;
    SV s[16];
    for (int i = 0; i < n; i++) {
        float z = MAX(v[i].z, 1.0f), iw = 1.0f / z;
        s[i].x = (cam_cx + cam_f * v[i].x * iw - bx0) * SS;
        s[i].y = (cam_cy - cam_f * v[i].y * iw - by0) * SS;
        s[i].iw = iw; s[i].uw = uv[i * 2] * iw; s[i].vw = uv[i * 2 + 1] * iw;
    }
    for (int i = 1; i + 1 < n; i++) raster_tri(&s[0], &s[i], &s[i + 1], m);
}

int r3d_end_img(Img *out, int *ox, int *oy) {
    if (!bw) return -1;
    if (img_alloc(out, bw, bh)) { bw = bh = 0; return -1; }
    int W = bw * SS;
    for (int y = 0; y < bh; y++) for (int x = 0; x < bw; x++) {
        const uint32_t *p = &buf[(size_t)(y * SS) * W + x * SS];
        uint32_t c[4] = { p[0], p[1], p[W], p[W + 1] };
        int a = 0, r = 0, g = 0, b = 0;
        for (int i = 0; i < 4; i++) { a += (int)CA(c[i]); r += (int)CR(c[i]); g += (int)CG(c[i]); b += (int)CB(c[i]); }
        if (a) out->px[(size_t)y * bw + x] = ARGB(a / 4, r * 255 / a, g * 255 / a, b * 255 / a);
    }
    *ox = bx0; *oy = by0;
    bw = bh = 0;
    return 0;
}

void r3d_end(int alpha) {
    if (!bw || alpha <= 0) return;
    int W = bw * SS;
    for (int y = 0; y < bh; y++) for (int x = 0; x < bw; x++) {
        const uint32_t *p = &buf[(size_t)(y * SS) * W + x * SS];
        uint32_t c[4] = { p[0], p[1], p[W], p[W + 1] };
        int a = 0, r = 0, g = 0, b = 0;
        for (int i = 0; i < 4; i++) { a += (int)CA(c[i]); r += (int)CR(c[i]); g += (int)CG(c[i]); b += (int)CB(c[i]); }
        if (!a) continue;
        /* premultiplied average -> straight colour + coverage */
        uint32_t col = RGB(r * 255 / a, g * 255 / a, b * 255 / a);
        pblend(target, bx0 + x, by0 + y, col, (a / 4) * alpha / 255);
    }
    bw = bh = 0;
}
