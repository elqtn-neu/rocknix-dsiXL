// cart.c: the cartridges. Each is its outline (convex, in millimetres) extruded to its thickness, the front face
// textured with a picture of the cartridge's face drawn here once per game: the plastic, the label, the art.
//   DS game card:        35.0 x 33.0 x 3.8 mm, dark grey, the label with its white NINTENDO DS strip at the top
//   GBA Game Pak:        57.5 x 35.0 x 8.0 mm, grey, a raised arch along the top edge, the label in a recess
#include "cart.h"
#include "r3d.h"
#include "font.h"
#include <stdlib.h>
#include <stdio.h>

#define PPM 4                       /* texture pixels per millimetre */

typedef struct { float x, y; } P2;

typedef struct {
    int sys;
    float w, h, d;
    P2 out[24]; int n;              /* the outline, counter-clockwise (y up), centred */
    uint32_t plastic;
} Model;

static Model model[SYS_COUNT];
static Img back_tex[SYS_COUNT];

void cart_size(int sys, float *w, float *h, float *d) {
    *w = sys == SYS_NDS ? 35.0f : 57.5f; *h = sys == SYS_NDS ? 33.0f : 35.0f; *d = sys == SYS_NDS ? 3.8f : 8.0f;
}

static void build_models(void) {
    static int done; if (done) return; done = 1;
    for (int s = 0; s < SYS_COUNT; s++) {
        Model *m = &model[s];
        m->sys = s; cart_size(s, &m->w, &m->h, &m->d);
        float hw = m->w / 2, hh = m->h / 2;
        int n = 0;
        if (s == SYS_NDS) {
            /* small chamfers, a bigger one at the top right (the card's keyed corner) */
            float c = 0.9f, k = 3.2f;
            P2 o[] = { { -hw + c, -hh }, { hw - c, -hh }, { hw, -hh + c }, { hw, hh - k }, { hw - k, hh }, { -hw + c, hh }, { -hw, hh - c }, { -hw, -hh + c } };
            for (int i = 0; i < ARRAY_LEN(o); i++) m->out[n++] = o[i];
            m->plastic = RGB(0x4a, 0x4c, 0x52);
        } else {
            /* rounded bottom corners, an arch across the top */
            float c = 1.2f;
            m->out[n++] = (P2){ -hw + c, -hh }; m->out[n++] = (P2){ hw - c, -hh }; m->out[n++] = (P2){ hw, -hh + c };
            m->out[n++] = (P2){ hw, hh - 2.6f };
            for (int i = 0; i <= 8; i++) {          /* the arch: from the right shoulder to the left */
                float t = i / 8.0f, x = hw - 0.8f - t * (m->w - 1.6f);
                float y = hh - 2.6f + 0.8f + 1.8f * sinf(t * PI_F);
                if (i == 0) y = hh - 1.8f; else if (i == 8) y = hh - 1.8f;
                m->out[n++] = (P2){ x, y };
            }
            m->out[n++] = (P2){ -hw, hh - 2.6f }; m->out[n++] = (P2){ -hw, -hh + c };
            m->plastic = RGB(0x8c, 0x8e, 0x96);
        }
        m->n = n;
    }
}

/* draw on an image as on a surface (the textures are opaque, so their alpha is set once at the end) */
static Surf as_surf(Img *im) { Surf s = { im->w, im->h, im->w, im->px, 0, 0, im->w, im->h }; return s; }
static void opaque(Img *im) { for (int i = 0; i < im->w * im->h; i++) im->px[i] |= 0xff000000u; }

/* the art for the label area: a label scan as is, box art cropped below its own header band, else something made
 * from what the ROM says (its icon, its title) */
static void label_art(Game *g, Surf *s, int x, int y, int w, int h) {
    if (g->label_state == 1 || g->label_state == 2) {
        Img src = g->label, crop, fit;
        if (g->label_state == 2) {
            /* the box's own band is the label's strip, drawn separately: across the top of a DS box, down the left
             * side of a GBA box */
            int top = g->sys == SYS_NDS ? g->label.h * 13 / 100 : 0, left = g->sys == SYS_GBA ? g->label.w * 15 / 100 : 0;
            if (img_alloc(&crop, g->label.w - left, g->label.h - top)) return;
            for (int y = 0; y < crop.h; y++) memcpy(crop.px + (size_t)y * crop.w, g->label.px + (size_t)(y + top) * g->label.w + left, (size_t)crop.w * 4);
            src = crop;
        }
        img_resize_cover(&src, &fit, w, h, 0.3f);
        if (g->label_state == 2) img_free(&crop);
        if (fit.px) { for (int i = 0; i < fit.w * fit.h; i++) fit.px[i] |= 0xff000000u; blit(s, &fit, x, y, 255); img_free(&fit); }
        return;
    }
    /* no art: a soft gradient in the game's own colour, its icon (DS) and its title */
    uint32_t h0 = hash_str(g->title), base;
    if (g->icon.px) {
        int r = 0, gg = 0, b = 0, n = 0;
        for (int i = 0; i < 32 * 32; i++) if (CA(g->icon.px[i])) { r += CR(g->icon.px[i]); gg += CG(g->icon.px[i]); b += CB(g->icon.px[i]); n++; }
        base = n ? RGB(r / n, gg / n, b / n) : RGB(120, 140, 170);
    } else {
        static const uint32_t pal[] = { 0x3d6fb6, 0xc0392b, 0x2e8b57, 0x8e44ad, 0xd35400, 0x16a085, 0x2c3e50, 0xb7950b };
        base = pal[h0 % ARRAY_LEN(pal)];
    }
    rect_vgrad(s, x, y, w, h, col_mix(base, 0xffffff, 150), col_mix(base, 0x000000, 40), 255);
    /* a faint pattern of dots, like a printed label */
    for (int yy = 0; yy < h; yy += 6) for (int xx = (yy / 6) % 2 * 3; xx < w; xx += 6) pblend(s, x + xx, y + yy, 0xffffff, 60);
    int ty = y + h / 2;
    if (g->icon.px) {
        int k = MIN(w, h) >= 90 ? 2 : 1;
        int ix = x + (w - 32 * k) / 2, iy = y + (h - 32 * k) / 2 - 10;
        rrect(s, ix - 4, iy - 4, 32 * k + 8, 32 * k + 8, 5, 0xffffff, 200);
        blit_scaled(s, &g->icon, ix, iy, 32 * k, 32 * k, 255, 0);
        ty = iy + 32 * k + 7;
    }
    char t[128]; text_fit(FONT_BD, g->title, w - 8, t, sizeof t);
    text_center(s, FONT_BD, x + w / 2 + 1, ty + 1, 0x000000, 90, t);
    text_center(s, FONT_BD, x + w / 2, ty, 0xffffff, 255, t);
}

/* per game: the face texture, and the last picture of the cartridge with the pose it was drawn in (a cartridge that
 * only bobs up and down is the same picture at another place: drawn once, then copied) */
typedef struct { Img front; Img spr; int ox, oy, has; CartPose key; } CartData;

static Img *build_front(Game *g, Img *im) {
    Model *m = &model[g->sys];
    if (img_alloc(im, (int)(m->w * PPM), (int)(m->h * PPM))) return 0;
    Surf s = as_surf(im);
    uint32_t pl = m->plastic;
    rect_vgrad(&s, 0, 0, im->w, im->h, col_scale(pl, 290), col_scale(pl, 230), 255);
    if (g->sys == SYS_NDS) {
        /* grip ridges along the top */
        for (int i = 0; i < 4; i++) hline(&s, 10, im->w - 22, 3 + i * 3, col_scale(pl, 200), 255), hline(&s, 10, im->w - 22, 4 + i * 3, col_scale(pl, 330), 200);
        int lx = 2 * PPM, ly = 4.6f * PPM, lw = im->w - 4 * PPM, lh = im->h - ly - 2 * PPM;
        rrect(&s, lx - 1, ly - 1, lw + 2, lh + 2, 4, col_scale(pl, 170), 255);       /* the label's edge in the plastic */
        Surf l = s; surf_clip(&l, lx, ly, lw, lh);
        int strip = 4 * PPM;
        rect_vgrad(&l, lx, ly, lw, strip, 0xffffff, 0xdedede, 255);
        text_center(&l, FONT_SM, lx + lw / 2, ly + 1, 0x202020, 255, "NINTENDO DS\xe2\x84\xa2");
        label_art(g, &l, lx, ly + strip, lw, lh - strip);
        /* the label's printed gloss: a lighter top edge */
        hline(&l, lx, lx + lw, ly + strip, 0x000000, 50);
    } else {
        /* the recessed label area, the raised arrow and "Nintendo" moulded in the plastic below it */
        int lx = 4 * PPM, ly = 6 * PPM, lw = im->w - 8 * PPM, lh = im->h - ly - 7 * PPM;
        rrect(&s, lx - 3, ly - 3, lw + 6, lh + 6, 6, col_scale(pl, 200), 255);
        rrect(&s, lx - 2, ly - 2, lw + 4, lh + 4, 5, col_scale(pl, 235), 255);
        Surf l = s; surf_clip(&l, lx, ly, lw, lh);
        int strip = 4 * PPM;
        rect_vgrad(&l, lx, ly, lw, strip, RGB(0x2a, 0x1e, 0x5c), RGB(0x14, 0x0e, 0x30), 255);
        text_center(&l, FONT_SM, lx + lw / 2, ly + 1, 0xffffff, 255, "GAME BOY ADVANCE");
        label_art(g, &l, lx, ly + strip, lw, lh - strip);
        int ny = im->h - 6 * PPM + 4;
        text_center(&s, FONT_BD, im->w / 2 + 1, ny + 1, col_scale(pl, 330), 255, "Nintendo");
        text_center(&s, FONT_BD, im->w / 2, ny, col_scale(pl, 190), 255, "Nintendo");
        float ax = im->w / 2.0f, ay = 2.2f * PPM;
        tri_fill(&s, ax - 6, ay - 3, ax + 6, ay - 3, ax, ay + 4, col_scale(pl, 200), 255);
    }
    opaque(im);
    return im;
}

static void build_backs(void) {
    static int done; if (done) return; done = 1;
    for (int sy = 0; sy < SYS_COUNT; sy++) {
        Model *m = &model[sy];
        Img *im = &back_tex[sy];
        if (img_alloc(im, (int)(m->w * PPM), (int)(m->h * PPM))) continue;
        Surf s = as_surf(im);
        rect_vgrad(&s, 0, 0, im->w, im->h, col_scale(m->plastic, 260), col_scale(m->plastic, 210), 255);
        /* the gold contacts along the bottom edge */
        int n = sy == SYS_NDS ? 17 : 32, cw = sy == SYS_NDS ? 6 : 5, gap = (im->w - 16) / n;
        for (int i = 0; i < n; i++) rect_vgrad(&s, 8 + i * gap + 1, im->h - 6 * PPM, cw, 5 * PPM, RGB(0xf5, 0xd7, 0x6e), RGB(0xb0, 0x86, 0x2a), 255);
        /* a sticker with the fine print */
        rrect(&s, im->w * 0.2f, PPM * 5, im->w * 0.6f, PPM * 9, 3, col_scale(m->plastic, 300), 255);
        opaque(im);
    }
}

void cart_free(Game *g) {
    CartData *cd = g->cart;
    if (!cd) return;
    img_free(&cd->front); img_free(&cd->spr); free(cd); g->cart = 0;
}

static int same_pose(const CartPose *a, const CartPose *b) {
    return fabsf(a->scale - b->scale) < 0.002f && fabsf(a->yaw - b->yaw) < 0.002f && fabsf(a->pitch - b->pitch) < 0.002f &&
           fabsf(a->roll - b->roll) < 0.002f && fabsf(a->desat - b->desat) < 0.01f && fabsf(a->bright - b->bright) < 0.01f &&
           a->gloss < 0.001f && b->gloss < 0.001f && fabsf(a->stretch_x - b->stretch_x) < 0.002f &&
           fabsf(a->stretch_y - b->stretch_y) < 0.002f;
}

static float face_shade(V3 n, float *spec) {
    V3 L = v3_norm(v3(-0.45f, 0.75f, -0.55f));       /* light: upper left, in front */
    V3 H = v3_norm(v3_add(L, v3(0, 0, -1)));          /* toward the viewer */
    float dif = MAX(0, v3_dot(n, L));
    *spec = powf(MAX(0, v3_dot(n, H)), 28) * 0.55f;
    return 0.58f + 0.55f * dif;
}

/* a face's normal, pointing away from the cartridge's centre */
static V3 outward(const V3 *q, V3 ctr) {
    V3 n = v3_norm(v3_cross(v3_sub(q[1], q[0]), v3_sub(q[2], q[0])));
    return v3_dot(n, v3_sub(q[0], ctr)) < 0 ? v3_scale(n, -1) : n;
}

void cart_draw(Surf *s, Game *g, const CartPose *p) {
    if (p->alpha <= 0.004f || p->scale <= 0.01f) return;
    build_models(); build_backs();
    if (g->label_state == 0) game_load_art(g);
    if (!g->cart) {
        CartData *cd = calloc(1, sizeof *cd);
        if (!cd) return;
        build_front(g, &cd->front);
        g->cart = cd;
    }
    CartData *cd = g->cart;
    /* the same pose as last time: the same picture, wherever it is now (to the whole pixel, as the DS moves things) */
    int rx = (int)roundf(p->sx), ry = (int)roundf(p->sy);
    if (cd->has && same_pose(&cd->key, p)) {
        blit(s, &cd->spr, rx + cd->ox, ry + cd->oy, (int)(p->alpha * 255));
        return;
    }
    int cache = p->gloss < 0.001f;
    Model *m = &model[g->sys];
    const float z0 = 260;                              /* the camera's distance: a natural perspective */
    r3d_camera(cache ? rx : p->sx, cache ? ry : p->sy, p->scale * z0);
    M3 R = m3_rot(p->yaw, p->pitch, p->roll);
    float kx = p->stretch_x > 0 ? p->stretch_x : 1, ky = p->stretch_y > 0 ? p->stretch_y : 1;
    V3 fr[24], bk[24];
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < m->n; i++) {
        /* squashed from its bottom edge (a cartridge lands on it): the bottom stays where it is */
        float ox = m->out[i].x * kx, oy = (m->out[i].y + m->h / 2) * ky - m->h / 2;
        fr[i] = v3_add(m3_mul(&R, v3(ox, oy, -m->d / 2)), v3(0, 0, z0));
        bk[i] = v3_add(m3_mul(&R, v3(ox, oy, m->d / 2)), v3(0, 0, z0));
        float x, y;
        r3d_project(fr[i], &x, &y); minx = MIN(minx, x); maxx = MAX(maxx, x); miny = MIN(miny, y); maxy = MAX(maxy, y);
        r3d_project(bk[i], &x, &y); minx = MIN(minx, x); maxx = MAX(maxx, x); miny = MIN(miny, y); maxy = MAX(maxy, y);
    }
    if (cache) { minx -= 1; maxx += 1; miny -= 1; maxy += 1; }
    if (r3d_begin(cache ? 0 : s, (int)floorf(minx) - 1, (int)floorf(miny) - 1, (int)ceilf(maxx) + 2, (int)ceilf(maxy) + 2)) return;
    float spec;
    V3 ctr = v3(0, 0, z0);
    Mat mat = { 0 };
    mat.desat = p->desat;
    /* the sides */
    for (int i = 0; i < m->n; i++) {
        int j = (i + 1) % m->n;
        V3 q[4] = { fr[i], fr[j], bk[j], bk[i] };
        V3 nrm = outward(q, ctr);
        if (v3_dot(nrm, q[0]) >= 0) continue;          /* facing away */
        float uv[8] = { 0 };
        mat.tex = 0; mat.color = col_scale(m->plastic, 245);
        mat.shade = face_shade(nrm, &spec) * p->bright; mat.spec = spec * 0.6f;
        r3d_poly(q, uv, 4, &mat);
    }
    /* the back (reversed: counter-clockwise seen from behind) and the front */
    V3 poly[24]; float uv[48];
    for (int pass = 0; pass < 2; pass++) {
        const V3 *src = pass ? fr : bk;
        for (int i = 0; i < m->n; i++) {
            int k = pass ? i : m->n - 1 - i;
            poly[i] = src[k];
            uv[i * 2] = (m->out[k].x + m->w / 2) / m->w;
            uv[i * 2 + 1] = (m->h / 2 - m->out[k].y) / m->h;
            if (!pass) uv[i * 2] = 1 - uv[i * 2];      /* seen from behind, mirrored */
        }
        V3 nrm = outward(poly, ctr);
        if (v3_dot(nrm, poly[0]) >= 0) continue;
        Img *tex = pass ? &cd->front : &back_tex[g->sys];
        mat.tex = tex && tex->px ? tex : 0; mat.color = m->plastic;
        mat.shade = face_shade(nrm, &spec) * p->bright; mat.spec = spec;
        mat.gloss = pass ? p->gloss : 0; mat.gloss_pos = p->gloss_pos; mat.sheen = pass ? 0.35f : 0;
        r3d_poly(poly, uv, m->n, &mat);
    }
    if (!cache) { r3d_end((int)(p->alpha * 255)); cd->has = 0; return; }
    img_free(&cd->spr);
    int ox, oy;
    if (r3d_end_img(&cd->spr, &ox, &oy)) { cd->has = 0; return; }
    cd->ox = ox - rx; cd->oy = oy - ry; cd->key = *p; cd->has = 1;
    blit(s, &cd->spr, ox, oy, (int)(p->alpha * 255));
}
