// cart3d.c: see cart3d.h. Each cartridge is its outline (convex, in millimetres) extruded to its thickness; the front
// face is textured with a picture of the cartridge drawn here once per game (the plastic, the label strip, the art):
//   DS game card:        35.0 x 33.0 x 3.8 mm, dark grey, the label with its white NINTENDO DS strip at the top
//   GBA Game Pak:        57.5 x 35.0 x 8.0 mm, grey, the label in a recess, "Nintendo" moulded under it
// A pose that is drawn again (a cartridge at rest) is the same picture: drawn once with smooth edges, then copied.
// One that changes every frame (a cartridge in motion) is drawn with one sample a pixel: at this resolution its
// edges are fine, and it's four times less work.
#include "cart3d.h"
#include "plat/plat.h"
#include "r3d.h"
#include "art.h"
#include "hgfx.h"
#include "hfont.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PPM 10                      /* the faces' pixels per millimetre */

typedef struct { float x, y; } P2;
typedef struct { int sys; float w, h, d; P2 out[24]; int n; uint32_t plastic; } Model;
static Model model[SYS_COUNT];
static Img back_tex[SYS_COUNT];

void cart3d_size(int sys, float *w, float *h, float *d) {
    *w = sys == SYS_NDS ? 35.0f : 57.5f; *h = sys == SYS_NDS ? 33.0f : 35.0f; *d = sys == SYS_NDS ? 3.8f : 8.0f;
}

static void build_models(void) {
    static int done; if (done) return; done = 1;
    for (int s = 0; s < SYS_COUNT; s++) {
        Model *m = &model[s];
        m->sys = s; cart3d_size(s, &m->w, &m->h, &m->d);
        float hw = m->w / 2, hh = m->h / 2;
        int n = 0;
        if (s == SYS_NDS) {
            /* small chamfers, a bigger one at the top right (the card's keyed corner) */
            float c = 0.9f, k = 3.2f;
            P2 o[] = { { -hw + c, -hh }, { hw - c, -hh }, { hw, -hh + c }, { hw, hh - k }, { hw - k, hh }, { -hw + c, hh }, { -hw, hh - c }, { -hw, -hh + c } };
            for (int i = 0; i < ARRAY_LEN(o); i++) m->out[n++] = o[i];
            m->plastic = RGB(0x46, 0x48, 0x4e);
        } else {
            /* a flat top with well-rounded corners (the grip end), small chamfers at the contacts' end */
            float c = 1.0f, r = 3.0f;
            m->out[n++] = (P2){ -hw + c, -hh }; m->out[n++] = (P2){ hw - c, -hh }; m->out[n++] = (P2){ hw, -hh + c };
            for (int i = 0; i <= 4; i++) { float a = i / 4.0f * PI_F / 2; m->out[n++] = (P2){ hw - r + r * cosf(a), hh - r + r * sinf(a) }; }
            for (int i = 0; i <= 4; i++) { float a = PI_F / 2 + i / 4.0f * PI_F / 2; m->out[n++] = (P2){ -hw + r + r * cosf(a), hh - r + r * sinf(a) }; }
            m->out[n++] = (P2){ -hw, -hh + c };
            m->plastic = RGB(0x9a, 0x9b, 0xa2);
        }
        m->n = n;
    }
}

static Surf as_surf(Img *im) { Surf s = { im->w, im->h, im->w, im->px, 0, 0, im->w, im->h }; return s; }
static void opaque(Img *im) { for (int i = 0; i < im->w * im->h; i++) im->px[i] |= 0xff000000u; }
static uint32_t shade(uint32_t c, float k) { return h_mix(c, k > 1 ? 0xffffff : 0x000000, k > 1 ? k - 1 : 1 - k); }

/* the art for the label area: a label scan as it is; box art cropped below its own header band; else something
 * made from what the ROM says (its colours, its icon, its title) */
static void label_art(Game *g, Surf *s, int x, int y, int w, int h) {
    Art *A = art_get(g);
    Img fit = { 0 };
    if (A && A->state == 1) {
        if (A->face_kind == 2 && A->face.px) hb_fit(&A->face, &fit, w, h, 1);
        else if (A->cover.px) {
            /* a box: what a label doesn't have cut away (its header band, the spine, the rating and publisher strip) */
            const Img *b = &A->cover; int W = b->w, H = b->h;
            int top = g->sys == SYS_NDS ? H * 13 / 100 : H * 3 / 100, left = g->sys == SYS_NDS ? W * 4 / 100 : W * 12 / 100;
            int bottom = H * 12 / 100, right = W * 2 / 100;
            Img crop;
            if (!img_alloc(&crop, W - left - right, H - top - bottom)) {
                for (int yy = 0; yy < crop.h; yy++) memcpy(crop.px + (size_t)yy * crop.w, b->px + (size_t)(yy + top) * W + left, (size_t)crop.w * 4);
                hb_fit(&crop, &fit, w, h, 1);
                img_free(&crop);
            }
        }
    }
    if (fit.px) { opaque(&fit); hb_blit(s, &fit, x, y, 255); img_free(&fit); return; }
    uint32_t v = A && A->state ? A->vivid : 0x5d7fb8, d = A && A->state ? A->deep : 0x26344d;
    rect_vgrad(s, x, y, w, h, h_mix(v, 0xffffff, 0.25f), d, 255);
    /* a faint printed pattern */
    for (int yy = 6; yy < h; yy += 14) for (int xx = 6 + (yy / 14) % 2 * 7; xx < w; xx += 14) hi_circle(s, x + xx, y + yy, 1.6f, 0, 0xffffff, 40);
    int ty = y + h / 2;
    if (g->icon.px) {
        int z = MIN(w, h) >= 180 ? 96 : 64;
        int ix = x + (w - z) / 2, iy = y + (h - z) / 2 - 22;
        hr_fill(s, ix - 8, iy - 8, z + 16, z + 16, 12, 0xffffff, 210);
        hb_scaled(s, &g->icon, (float)ix, (float)iy, (float)z, (float)z, 255);
        ty = iy + z + 14;
    } else ty = y + h / 2 - ht_h(HF_LABEL);
    Surf c = *s; surf_clip(&c, x + 6, y, w - 12, h);
    ht_wrap(&c, HF_LABEL, x + 10, ty, w - 20, ht_h(HF_LABEL), 2, 0xffffff, 245, g->title);
}

static void build_front(Game *g, Img *im) {
    Model *m = &model[g->sys];
    if (img_alloc(im, (int)(m->w * PPM), (int)(m->h * PPM))) return;
    Surf s = as_surf(im);
    uint32_t pl = m->plastic;
    rect_vgrad(&s, 0, 0, im->w, im->h, shade(pl, 1.12f), shade(pl, 0.9f), 255);
    Art *A = art_get(g);
    if (A && A->state == 1 && A->face_kind == 1 && A->face.px) {
        /* a scan of the real cartridge: its whole face, as it is */
        Img fit = { 0 }; hb_fit(&A->face, &fit, im->w, im->h, 0);
        if (fit.px) { hb_blit(&s, &fit, 0, 0, 255); img_free(&fit); }
    } else if (g->sys == SYS_NDS) {
        /* the grip ridges along the top */
        for (int i = 0; i < 4; i++) {
            hr_fill(&s, 26, 8 + i * 8, im->w - 80, 3, 1, shade(pl, 0.72f), 255);
            hr_fill(&s, 26, 11 + i * 8, im->w - 80, 2, 1, shade(pl, 1.3f), 200);
        }
        int lx = 2 * PPM, ly = (int)(4.6f * PPM), lw = im->w - 4 * PPM, lh = im->h - ly - 2 * PPM;
        hr_fill(&s, lx - 3, ly - 3, lw + 6, lh + 6, 10, shade(pl, 0.62f), 255);     /* the label's edge in the plastic */
        Surf l = s; surf_clip(&l, lx, ly, lw, lh);
        int strip = 4 * PPM;
        rect_vgrad(&l, lx, ly, lw, strip, 0xffffff, 0xdcdcdc, 255);
        ht_center(&l, HF_CAPS, lx + lw / 2, ly + strip / 2 - ht_h(HF_CAPS) / 2, 0x202020, 255, 1, "NINTENDO DS\xe2\x84\xa2");
        label_art(g, &l, lx, ly + strip, lw, lh - strip);
        rect_blend(&l, lx, ly + strip, lw, 2, 0x000000, 60);
        /* the label's printed gloss: a lighter top half */
        rect_vgrad(&l, lx, ly + strip, lw, (lh - strip) / 2, 0xffffff, 0xffffff, 0);
    } else {
        /* the recessed label area; the arrow and "Nintendo" moulded in the plastic below it */
        int lx = 4 * PPM, ly = 6 * PPM, lw = im->w - 8 * PPM, lh = im->h - ly - 7 * PPM;
        hr_fill(&s, lx - 8, ly - 8, lw + 16, lh + 16, 16, shade(pl, 0.78f), 255);
        hr_fill(&s, lx - 5, ly - 5, lw + 10, lh + 10, 13, shade(pl, 0.92f), 255);
        Surf l = s; surf_clip(&l, lx, ly, lw, lh);
        int strip = 4 * PPM;
        rect_vgrad(&l, lx, ly, lw, strip, RGB(0x2a, 0x1e, 0x5c), RGB(0x14, 0x0e, 0x30), 255);
        ht_center(&l, HF_CAPS, lx + lw / 2, ly + strip / 2 - ht_h(HF_CAPS) / 2, 0xffffff, 255, 2, "GAME BOY ADVANCE");
        label_art(g, &l, lx, ly + strip, lw, lh - strip);
        int ny = im->h - 6 * PPM + 8;
        ht_center(&s, HF_TITLE, im->w / 2 + 2, ny + 2, shade(pl, 1.28f), 255, 1, "Nintendo");
        ht_center(&s, HF_TITLE, im->w / 2, ny, shade(pl, 0.74f), 255, 1, "Nintendo");
        float ax = im->w / 2.0f, ay = 2.2f * PPM;
        float tri[6] = { ax - 16, ay - 8, ax + 16, ay - 8, ax, ay + 11 };
        hi_poly(&s, tri, 3, shade(pl, 0.78f), 255);
    }
    opaque(im);
}

static void build_backs(void) {
    static int done; if (done) return; done = 1;
    for (int sy = 0; sy < SYS_COUNT; sy++) {
        Model *m = &model[sy];
        Img *im = &back_tex[sy];
        if (img_alloc(im, (int)(m->w * PPM), (int)(m->h * PPM))) continue;
        Surf s = as_surf(im);
        rect_vgrad(&s, 0, 0, im->w, im->h, shade(m->plastic, 1.02f), shade(m->plastic, 0.82f), 255);
        /* the gold contacts along the bottom edge */
        int n = sy == SYS_NDS ? 17 : 32, gap = (im->w - 40) / n, cw = gap * 2 / 3;
        for (int i = 0; i < n; i++) hr_vgrad(&s, 20 + i * gap + 2, im->h - 6 * PPM, cw, 5 * PPM, 3, RGB(0xf5, 0xd7, 0x6e), RGB(0xb0, 0x86, 0x2a), 255);
        /* a sticker with the fine print */
        hr_fill(&s, (int)(im->w * 0.2f), PPM * 5, (int)(im->w * 0.6f), PPM * 9, 10, shade(m->plastic, 1.2f), 255);
        for (int i = 0; i < 4; i++) hr_fill(&s, (int)(im->w * 0.25f), PPM * 6 + i * 18, (int)(im->w * (0.5f - 0.08f * (i & 1))), 6, 3, shade(m->plastic, 0.95f), 255);
        opaque(im);
    }
}

/* per game: the face, a half-size copy (for small ones: no shimmer), the last pictures with their poses */
#define SLOTS 3
typedef struct {
    Img front, half;
    Img spr[SLOTS]; int ox[SLOTS], oy[SLOTS], has[SLOTS]; unsigned use[SLOTS]; CartPose key[SLOTS];
    CartPose last; int has_last;       /* the pose asked for last: asked again, it's at rest (drawn smooth, kept) */
    unsigned touched;
} CartData;
static unsigned use_clock;
static int live;                        /* how many games have one (a few dozen kept) */

void cart3d_forget(Game *g) {
    CartData *cd = g->cart;
    if (!cd) return;
    img_free(&cd->front); img_free(&cd->half);
    for (int k = 0; k < SLOTS; k++) img_free(&cd->spr[k]);
    free(cd); g->cart = 0; live--;
}
static void evict(void) {
    while (live > 40) {
        Game *old = 0;
        for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) {
            CartData *cd = lib[s].g[i].cart;
            if (cd && (!old || cd->touched < ((CartData *)old->cart)->touched)) old = &lib[s].g[i];
        }
        if (!old) break;
        cart3d_forget(old);
    }
}

int cart3d_prepare(Game *g) {
    if (g->cart) return 0;
    build_models(); build_backs();
    CartData *cd = calloc(1, sizeof *cd);
    if (!cd) return 0;
    build_front(g, &cd->front);
    if (cd->front.px) hb_fit(&cd->front, &cd->half, cd->front.w / 2, cd->front.h / 2, 1);
    g->cart = cd; live++;
    cd->touched = ++use_clock;
    evict();
    return 1;
}

static int same_pose(const CartPose *a, const CartPose *b) {
    return fabsf(a->scale - b->scale) < 0.001f && fabsf(a->yaw - b->yaw) < 0.001f && fabsf(a->pitch - b->pitch) < 0.001f &&
           fabsf(a->roll - b->roll) < 0.001f && fabsf(a->desat - b->desat) < 0.005f && fabsf(a->bright - b->bright) < 0.005f &&
           fabsf(a->gloss - b->gloss) < 0.001f && (a->gloss < 0.001f || fabsf(a->gloss_pos - b->gloss_pos) < 0.001f) &&
           fabsf(a->stretch_x - b->stretch_x) < 0.001f && fabsf(a->stretch_y - b->stretch_y) < 0.001f &&
           fabsf((a->sx - floorf(a->sx)) - (b->sx - floorf(b->sx))) < 0.26f && fabsf((a->sy - floorf(a->sy)) - (b->sy - floorf(b->sy))) < 0.26f;
}

static float face_shade(V3 n, float *spec) {
    V3 L = v3_norm(v3(-0.45f, 0.75f, -0.55f));       /* light: upper left, in front */
    V3 H = v3_norm(v3_add(L, v3(0, 0, -1)));          /* toward the viewer */
    float dif = MAX(0, v3_dot(n, L));
    *spec = powf(MAX(0, v3_dot(n, H)), 28) * 0.55f;
    return 0.58f + 0.55f * dif;
}
static V3 outward(const V3 *q, V3 ctr) {
    V3 n = v3_norm(v3_cross(v3_sub(q[1], q[0]), v3_sub(q[2], q[0])));
    return v3_dot(n, v3_sub(q[0], ctr)) < 0 ? v3_scale(n, -1) : n;
}

/* the picture: the cartridge rendered into an image (its top-left at ox, oy, from its centre on whole pixels) */
static int render(Game *g, const CartPose *p, int ss, Img *out, int *ox, int *oy) {
    CartData *cd = g->cart;
    Model *m = &model[g->sys];
    const float z0 = 260;                              /* the camera's distance (mm): a natural perspective */
    float fx = p->sx - floorf(p->sx), fy = p->sy - floorf(p->sy);
    r3d_camera(fx, fy, p->scale * z0);                 /* (relative to the whole pixel: the picture moves by whole pixels) */
    M3 R = m3_rot(p->yaw, p->pitch, p->roll);
    float kx = p->stretch_x > 0 ? p->stretch_x : 1, ky = p->stretch_y > 0 ? p->stretch_y : 1;
    V3 fr[24], bk[24];
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < m->n; i++) {
        float x = m->out[i].x * kx, y = (m->out[i].y + m->h / 2) * ky - m->h / 2;   /* squashed onto its bottom edge */
        fr[i] = v3_add(m3_mul(&R, v3(x, y, -m->d / 2)), v3(0, 0, z0));
        bk[i] = v3_add(m3_mul(&R, v3(x, y, m->d / 2)), v3(0, 0, z0));
        float px, py;
        r3d_project(fr[i], &px, &py); minx = MIN(minx, px); maxx = MAX(maxx, px); miny = MIN(miny, py); maxy = MAX(maxy, py);
        r3d_project(bk[i], &px, &py); minx = MIN(minx, px); maxx = MAX(maxx, px); miny = MIN(miny, py); maxy = MAX(maxy, py);
    }
    r3d_quality(ss);
    int bx = (int)floorf(minx) - 2, by = (int)floorf(miny) - 2;
    if (!ss) { bx -= ((int)floorf(p->sx) + bx) & 1; by -= ((int)floorf(p->sy) + by) & 1; }   /* (its 2x2 blocks on the LCD's) */
    if (r3d_begin(0, bx, by, (int)ceilf(maxx) + 3, (int)ceilf(maxy) + 3)) return -1;
    float spec;
    V3 ctr = v3(0, 0, z0);
    Mat mat; memset(&mat, 0, sizeof mat);
    mat.desat = p->desat;
    /* the sides */
    for (int i = 0; i < m->n; i++) {
        int j = (i + 1) % m->n;
        V3 q[4] = { fr[i], fr[j], bk[j], bk[i] };
        V3 nrm = outward(q, ctr);
        if (v3_dot(nrm, q[0]) >= 0) continue;
        float uv[8] = { 0 };
        mat.tex = 0; mat.color = h_mix(m->plastic, 0x000000, 0.04f);
        mat.shade = face_shade(nrm, &spec) * p->bright; mat.spec = spec * 0.6f;
        r3d_poly(q, uv, 4, &mat);
    }
    /* the back (seen from behind: mirrored) and the front */
    V3 poly[24]; float uv[48];
    for (int pass = 0; pass < 2; pass++) {
        const V3 *src = pass ? fr : bk;
        for (int i = 0; i < m->n; i++) {
            int k = pass ? i : m->n - 1 - i;
            poly[i] = src[k];
            uv[i * 2] = (m->out[k].x + m->w / 2) / m->w;
            uv[i * 2 + 1] = (m->h / 2 - m->out[k].y) / m->h;
            if (!pass) uv[i * 2] = 1 - uv[i * 2];
        }
        V3 nrm = outward(poly, ctr);
        if (v3_dot(nrm, poly[0]) >= 0) continue;
        Img *tex = pass ? (p->scale * m->w < cd->front.w * 0.55f && cd->half.px ? &cd->half : &cd->front) : &back_tex[g->sys];
        mat.tex = tex && tex->px ? tex : 0; mat.color = m->plastic; mat.nearest = 0;
        mat.shade = face_shade(nrm, &spec) * p->bright; mat.spec = spec;
        mat.gloss = pass ? p->gloss : 0; mat.gloss_pos = p->gloss_pos; mat.sheen = pass ? 0.35f : 0;
        r3d_poly(poly, uv, m->n, &mat);
    }
    return r3d_end_img(out, ox, oy);
}

/* the reflection: the picture upside down under its bottom edge, fading out */
static void reflect(Surf *s, const Img *im, int x, int y, int floor_y, float k, int alpha) {
    int H = MIN(im->h, (int)(im->h * 0.45f));
    for (int j = 0; j < H; j++) {
        int yy = floor_y + j; if (yy < s->cy0 || yy >= s->cy1) continue;
        int sy = (floor_y - y) - 1 - j;                /* mirrored about the floor */
        if (sy < 0 || sy >= im->h) continue;
        int a = (int)(alpha * k * (1 - (float)j / H) * (1 - (float)j / H));
        if (a <= 2) break;
        const uint32_t *r = im->px + (size_t)sy * im->w;
        uint32_t *d = s->px + (size_t)yy * s->pitch;
        for (int i = MAX(0, s->cx0 - x); i < MIN(im->w, s->cx1 - x); i++) {
            uint32_t c = r[i]; int ca = (int)(c >> 24) * a / 255;
            if (!ca) continue;
            uint32_t e = d[x + i], kk = (uint32_t)ca + 1, ik = 256 - kk;
            d[x + i] = ((((c & 0xff00ff) * kk + (e & 0xff00ff) * ik) >> 8) & 0xff00ff) | ((((c & 0x00ff00) * kk + (e & 0x00ff00) * ik) >> 8) & 0x00ff00);
        }
    }
}

void cart3d_draw(Surf *s, Game *g, const CartPose *p) {
    if (p->alpha <= 0.004f || p->scale <= 0.01f) return;
    cart3d_prepare(g);
    CartData *cd = g->cart;
    if (!cd) return;
    cd->touched = ++use_clock;
    int rx = (int)floorf(p->sx), ry = (int)floorf(p->sy), a = (int)(p->alpha * 255);
    float cw, ch, cdd; cart3d_size(g->sys, &cw, &ch, &cdd);
    int floor_y = (int)(ry + ch * p->scale * 0.5f * (p->stretch_y > 0 ? p->stretch_y : 1) * cosf(p->pitch) + 1);
    /* a pose drawn before: that picture */
    for (int k = 0; k < SLOTS; k++) if (cd->has[k] && same_pose(&cd->key[k], p)) {
        cd->use[k] = ++use_clock;
        hb_blit(s, &cd->spr[k], rx + cd->ox[k], ry + cd->oy[k], a);
        if (p->reflect > 0) reflect(s, &cd->spr[k], rx + cd->ox[k], ry + cd->oy[k], floor_y, p->reflect, a);
        cd->last = *p; cd->has_last = 1;
        return;
    }
    /* asked for twice in a row: at rest, drawn smooth and kept; else in motion, drawn fast */
    int rest = cd->has_last && same_pose(&cd->last, p);
    cd->last = *p; cd->has_last = 1;
    Img im = { 0 }; int ox, oy;
    /* (in motion on the DSi LCD look, which shows 2x2 pixels as one: a sample each 2x2, a quarter of the work) */
    if (render(g, p, rest ? 2 : plat_lcd ? 0 : 1, &im, &ox, &oy)) return;
    hb_blit(s, &im, rx + ox, ry + oy, a);
    if (p->reflect > 0) reflect(s, &im, rx + ox, ry + oy, floor_y, p->reflect, a);
    if (rest) {
        int k = 0; for (int i = 1; i < SLOTS; i++) if (!cd->has[i] || (cd->has[k] && cd->use[i] < cd->use[k])) k = i;
        img_free(&cd->spr[k]);
        cd->spr[k] = im; cd->ox[k] = ox; cd->oy[k] = oy; cd->key[k] = *p; cd->has[k] = 1; cd->use[k] = ++use_clock;
    } else img_free(&im);
}
