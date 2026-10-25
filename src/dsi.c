// dsi.c: see dsi.h. The measurements are the DSi's (in its pixels, 256 x 192 a screen) times four, so on the panels'
// LCD (two of its cells to a DS pixel) everything sits where the DSi has it, twice as fine.
#define _GNU_SOURCE
#include "dsi.h"
#include "hgfx.h"
#include "hfont.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define K 4                         /* panel pixels per DS pixel */
#define W 1024
#define H 768

Accent dsi_accent_of(uint32_t base) {
    Accent a;
    a.base = base;
    a.dark = h_mix(base, 0x000000, 0.31f);
    a.mid = h_mix(base, 0xffffff, 0.16f);
    a.light = h_mix(base, 0xffffff, 0.47f);
    a.pale = h_mix(base, 0xffffff, 0.80f);
    return a;
}
Accent dsi_accent(void) { return dsi_accent_of(fav_color(cfg.color)); }

/* ---------------------------------------------------------------- backgrounds */
/* each kind of background made once (per colour) a pattern-period wider than the screen, then copied from the offset
 * its pattern has slid to: a moving pattern costs a copy */
#define PER 128                     /* the pattern repeats every 32 DS pixels */
typedef struct { int kind; uint32_t key; Surf s; } Bg;
static Bg bgs[6]; static int bg_next;
static void diamond(Surf *s, float cx, float cy, uint32_t c, int a) {
    float r = 13;
    float p[8] = { cx, cy - r, cx + r, cy, cx, cy + r, cx - r, cy };
    hi_poly(s, p, 4, c, a);
    float q[8] = { cx, cy - r * 0.55f, cx + r * 0.55f, cy, cx, cy + r * 0.55f, cx - r * 0.55f, cy };
    hi_poly(s, q, 4, c, a / 2);
}
static void make_bg(Surf *s, int kind, uint32_t tint, float amt) {
    uint32_t a, b, pc; int pa;
    switch (kind) {
    case 0: a = 0xebebeb; b = 0xf3f3f3; pc = 0xaab2ba; pa = 26; break;
    case 1: a = h_mix(0xf2f2f2, h_mix(tint, 0xffffff, 0.59f), amt); b = h_mix(0xf9f9f9, h_mix(tint, 0xffffff, 0.75f), amt);
            pc = h_mix(0xb9c4cc, h_mix(tint, 0x606060, 0.25f), amt); pa = 30; break;
    case 2: a = h_mix(0xf6f0e2, h_mix(tint, 0xffffff, 0.67f), amt * 0.6f); b = h_mix(0xfcf8ee, h_mix(tint, 0xffffff, 0.8f), amt * 0.6f);
            pc = 0xd8b45a; pa = 44; break;
    default: a = h_mix(0x1a1d22, tint, amt * 0.12f); b = h_mix(0x20242a, tint, amt * 0.12f); pc = h_mix(0x3a414b, tint, 0.2f); pa = 60; break;
    }
    for (int y = 0; y < s->h; y++) {
        uint32_t c = (y / K) & 1 ? b : a, *r = s->px + (size_t)y * s->pitch;
        for (int x = 0; x < s->w; x++) r[x] = c;
    }
    for (int cy = -PER; cy < s->h + PER; cy += PER / 2) {
        int row = ((cy + 4 * PER) / (PER / 2)) & 1;
        for (int cx = -PER; cx < s->w + PER; cx += PER) {
            float x = cx + row * PER / 2 + PER / 4, y = cy + PER / 4;
            if (kind == 2) dsi_star(s, x, y, 15, pc, pa);
            else diamond(s, x, y, pc, pa);
        }
    }
}
static void bg(Surf *s, int kind, uint32_t tint, float amt, float par) {
    uint32_t key = kind == 0 ? 0 : (tint & 0xf8f8f8) ^ ((uint32_t)(amt * 31) << 24);
    Bg *b = 0;
    for (int i = 0; i < 6; i++) if (bgs[i].s.px && bgs[i].kind == kind && bgs[i].key == key) b = &bgs[i];
    if (!b) {
        b = &bgs[bg_next]; bg_next = (bg_next + 1) % 6;
        if (!b->s.px) surf_alloc(&b->s, W + PER, H);
        if (!b->s.px) { fill(s, 0xf2f2f2); return; }
        b->kind = kind; b->key = key;
        make_bg(&b->s, kind, tint, amt);
    }
    int off = ((int)floorf(par) % PER + PER) % PER;
    int x0 = MAX(s->cx0, 0), x1 = MIN(s->cx1, W), y1 = MIN(s->cy1, H);
    if (x1 <= x0) return;
    for (int y = MAX(s->cy0, 0); y < y1; y++)
        memcpy(s->px + (size_t)y * s->pitch + x0, b->s.px + (size_t)y * b->s.pitch + off + x0, (size_t)(x1 - x0) * 4);
}
void dsi_bg_top(Surf *s, uint32_t tint, float amt) { bg(s, 0, tint, amt, 0); }
void dsi_bg_bottom(Surf *s, uint32_t tint, float amt, float par) { bg(s, 1, tint, amt, par); }
void dsi_bg_favs(Surf *s, uint32_t tint, float amt, float par) { bg(s, 2, tint, amt, par); }
void dsi_bg_dark(Surf *s, uint32_t tint, float amt, float par) { bg(s, 3, tint, amt, par); }

/* ---------------------------------------------------------------- small things */
void dsi_glyph(Surf *s, int x, int y, const char *letter, uint32_t bg_, int alpha) {
    hr_fill(s, x, y, 44, 44, 14, bg_, alpha);
    hr_fill(s, x + 2, y + 2, 40, 18, 10, 0xffffff, alpha * 40 / 255);
    ht_center(s, HF_BD, x + 22, y + 22 - ht_h(HF_BD) / 2 - 1, 0xffffff, alpha, 0, letter);
}

void dsi_star(Surf *s, float cx, float cy, float r, uint32_t c, int alpha) {
    if (r < 1 || alpha <= 0) return;
    for (int pass = 0; pass < 2; pass++) {
        float rr = pass ? r : r + 5, ri = rr * 0.45f, p[20];
        for (int i = 0; i < 10; i++) {
            float a = -PI_F / 2 + i * PI_F / 5, rad = (i & 1) ? ri : rr;
            p[2 * i] = cx + cosf(a) * rad; p[2 * i + 1] = cy + sinf(a) * rad;
        }
        hi_poly(s, p, 10, pass ? c : 0xffffff, pass ? alpha : alpha * 230 / 255);
    }
    hi_circle(s, cx - r * 0.25f, cy - r * 0.3f, r * 0.14f, 0, 0xffffff, alpha * 150 / 255);
}

void dsi_sparkle(Surf *s, float x, float y, float size, uint32_t c, int alpha) {
    if (size <= 0 || alpha <= 0) return;
    uint32_t core = h_mix(c, 0xffffff, 0.66f);
    float r = size * K;
    hi_line(s, x - r, y, x + r, y, 4, c, alpha);
    hi_line(s, x, y - r, x, y + r, 4, c, alpha);
    hi_line(s, x - r * 0.45f, y, x + r * 0.45f, y, 6, core, alpha);
    hi_line(s, x, y - r * 0.45f, x, y + r * 0.45f, 6, core, alpha);
    hi_circle(s, x, y, 4, 0, 0xffffff, alpha);
}

void dsi_balloon(Surf *s, float cx, float tip_y, const char *txt, int alpha) {
    int tw = ht_w(HF_MD, txt, 0), w = tw + 88, h = 60;
    float x = roundf(cx - w / 2.0f), y = roundf(tip_y - 16 - h);
    hr_shadow(s, (int)x, (int)y, w, h, 24, 10, 6, alpha * 40 / 255);
    hr_fill(s, (int)x, (int)y, w, h, 24, 0xb9b2a0, alpha);
    hr_vgrad(s, (int)x + 4, (int)y + 4, w - 8, h - 8, 20, 0xffffff, 0xf6f1e4, alpha);
    float t1[6] = { cx - 16, y + h - 4, cx + 16, y + h - 4, cx, tip_y };
    hi_poly(s, t1, 3, 0xb9b2a0, alpha);
    float t2[6] = { cx - 11, y + h - 6, cx + 11, y + h - 6, cx, tip_y - 6 };
    hi_poly(s, t2, 3, 0xf6f1e4, alpha);
    float pl[6] = { x + 26, y + 18, x + 26, y + h - 18, x + 46, y + h / 2.0f };
    hi_poly(s, pl, 3, dsi_accent().base, alpha);
    ht(s, HF_MD, (int)x + 58, (int)y + h / 2 - ht_h(HF_MD) / 2, 0x5a4a28, alpha, 0, txt);
}

/* ---------------------------------------------------------------- tabs, the frame, the scroll bar */
void dsi_tab(Surf *s, int x, int y, int w, const char *glyph, int glyph_right, const char *label, int count, int active, float press, int alpha) {
    int h = 68, py = y + (int)(press * 6);
    uint32_t ft = active ? 0xffffff : 0xe6e6e6, fb = active ? 0xf0f0f0 : 0xd6d6d6;
    int star = label && !strcmp(label, "*");
    if (star && active) { ft = 0xfffdf6; fb = 0xf6ecd2; }
    /* hanging from the top edge: square at the top (off the screen), rounded at the bottom */
    hr_shadow(s, x, py - 24, w, h + 24, 24, 10, 6, alpha * 50 / 255);
    hr_fill(s, x, py - 24, w, h + 24, 24, active ? (star ? 0xcfae62 : 0xb4b4b4) : 0xc6c6c6, alpha);
    hr_vgrad(s, x + 4, py - 24, w - 8, h + 20, 20, ft, fb, alpha * (active ? 255 : 215) / 255);
    uint32_t tc = active ? D_TEXT : 0x9a9a9a;
    int gw = glyph ? 44 + 16 : 0;
    int lw = star ? 44 : ht_w(HF_BD, label, 0);
    char cnt[16] = ""; int cw = 0;
    if (count >= 0 && !star) { snprintf(cnt, sizeof cnt, "%d", count); cw = ht_w(HF_XS, cnt, 0) + 10; }
    int total = gw + lw + cw, x0 = x + (w - total) / 2, ty = py + h / 2 - ht_h(HF_BD) / 2 - 2;
    if (glyph && !glyph_right) { dsi_glyph(s, x0, py + h / 2 - 24, glyph, active ? 0x3c3c3c : 0xa0a0a0, alpha); x0 += gw; }
    if (star) dsi_star(s, x0 + 22, py + h / 2 - 2, 20, active ? D_STAR : 0xb0b0b0, alpha);
    else ht(s, HF_BD, x0, ty, tc, alpha, 0, label);
    x0 += lw;
    if (cw) { ht(s, HF_XS, x0 + 10, ty + 8, active ? dsi_accent().base : 0xaaaaaa, alpha, 0, cnt); x0 += cw; }
    if (glyph && glyph_right) dsi_glyph(s, x0 + 16, py + h / 2 - 24, glyph, active ? 0x3c3c3c : 0xa0a0a0, alpha);
    if (active) hr_fill(s, x + 24, py + h - 10, w - 48, 8, 4, star ? D_GOLD : dsi_accent().base, alpha * 220 / 255);
}

void dsi_frame(Surf *s, float cx, float cy, float w, float h, const char *label, float shine, float press, int alpha) {
    Accent a = dsi_accent();
    w -= press * 8; h -= press * 8;
    int x = (int)(cx - w / 2), y = (int)(cy - h / 2 + press * 4), iw = (int)w, ih = (int)h;
    hr_shadow(s, x, y, iw, ih, 28, 18, 8, alpha * 70 / 255);
    hr_fill(s, x, y, iw, ih, 28, a.dark, alpha);
    hr_vgrad(s, x + 4, y + 4, iw - 8, ih - 8, 24, a.light, h_mix(a.base, 0x000000, 0.12f), alpha);
    hr_vgrad(s, x + 8, y + 8, iw - 16, (int)(ih * 0.42f), 20, 0xffffff, 0xffffff, alpha * 60 / 255);   /* the gloss */
    hr_ring(s, x + 4, y + 4, iw - 8, ih - 8, 24, 3, 0xffffff, alpha * 80 / 255);
    if (shine > 0 && shine < 1) {
        /* a light passing over it */
        float pos = -ih * 0.6f + shine * (iw + ih * 1.2f);
        Surf c = *s; surf_clip(&c, x + 4, y + 4, iw - 8, ih - 8);
        for (int j = 0; j < 3; j++) {
            float o = pos + (j - 1) * 14;
            float p[8] = { x + o, (float)y, x + o + 26, (float)y, x + o + 26 - ih * 0.6f, (float)(y + ih), x + o - ih * 0.6f, (float)(y + ih) };
            hi_poly(&c, p, 4, 0xffffff, (int)(alpha * (j == 1 ? 0.30f : 0.12f)));
        }
    }
    if (label && *label) {
        int ty = y + ih - 72 + 36 - ht_h(HF_BD) / 2;
        ht_center(s, HF_BD, (int)cx + 3, ty + 3, a.dark, alpha * 200 / 255, 2, label);
        ht_center(s, HF_BD, (int)cx, ty, 0xffffff, alpha, 2, label);
    }
}

void dsi_scroll_bar(Surf *s, int y, int count, float pos, int press, const uint8_t *marks, int alpha) {
    Accent a = dsi_accent();
    int h = DSB_H;
    rect_vgrad(s, 0, y, W, h, 0xf2f2f2, 0xc6c6c6, alpha);
    rect_blend(s, 0, y, W, 4, 0xa0a0a0, alpha);
    rect_blend(s, 0, y + 4, W, 4, 0xffffff, alpha * 160 / 255);
    for (int side = 0; side < 2; side++) {
        int bx = side ? W - DSB_BTN : 0, pr = press == (side ? 1 : -1);
        uint32_t t = pr ? a.dark : a.light, b = pr ? a.base : h_mix(a.base, 0x000000, 0.16f);
        rect_vgrad(s, bx, y + 4, DSB_BTN, h - 4, t, b, alpha);
        rect_blend(s, side ? bx : bx + DSB_BTN - 4, y + 4, 4, h - 4, a.dark, alpha);
        rect_blend(s, bx + 4, y + 8, DSB_BTN - 8, (h - 8) / 2, 0xffffff, alpha * 45 / 255);
        float cx = bx + DSB_BTN / 2.0f + (pr ? 2 : 0), cy = y + h / 2.0f + 2 + (pr ? 2 : 0);
        float p[6];
        if (side) { p[0] = cx - 14; p[1] = cy - 20; p[2] = cx - 14; p[3] = cy + 20; p[4] = cx + 18; p[5] = cy; }
        else { p[0] = cx + 14; p[1] = cy - 20; p[2] = cx + 14; p[3] = cy + 20; p[4] = cx - 18; p[5] = cy; }
        hi_poly(s, p, 3, 0xffffff, alpha);
    }
    int tx0 = DSB_BTN + 24, tx1 = W - DSB_BTN - 24, tw = tx1 - tx0, my = y + h / 2 + 2;
    for (int x = tx0; x < tx1; x += 16) hr_fill(s, x, my - 2, 4, 4, 2, 0x9a9a9a, alpha);
    if (count <= 0) return;
    float step = MIN(24.0f, (float)tw / count), start = tx0 + (tw - step * count) / 2;
    for (int i = 0; i < count; i++) {
        int x = (int)(start + i * step);
        uint32_t mc = marks && marks[i] ? 0xe0a010 : 0x6e766e;
        if (step >= 16) hr_fill(s, x, my - 6, 12, 12, 3, mc, alpha);
        else rect_blend(s, x, my - 6, MAX(2, (int)step - 2), 12, mc, alpha);
    }
    int sel = CLAMP((int)roundf(pos), 0, count - 1);
    float vw = MAX(56.0f, 5 * step + 24), vx = start + pos * step + 6 - vw / 2;
    vx = clampf(vx, tx0 - 16, tx1 + 16 - vw);
    hr_fill(s, (int)vx, y + 10, (int)vw, h - 14, 10, 0xffffff, alpha * 150 / 255);
    hr_ring(s, (int)vx, y + 10, (int)vw, h - 14, 10, 4, a.mid, alpha);
    int sx = (int)(start + sel * step);
    hr_fill(s, sx, my - 6, step >= 16 ? 12 : MAX(2, (int)step - 2), 12, 3, D_BLUE, alpha);
}

int dsi_scroll_hit(int x, int count, int *zone) {
    if (x < DSB_BTN) { *zone = -1; return -1; }
    if (x >= W - DSB_BTN) { *zone = 1; return -1; }
    *zone = 0;
    int tx0 = DSB_BTN + 24, tx1 = W - DSB_BTN - 24, tw = tx1 - tx0;
    if (count <= 0) return -1;
    float step = MIN(24.0f, (float)tw / count), start = tx0 + (tw - step * count) / 2;
    return CLAMP((int)((x - start) / step), 0, count - 1);
}

/* ---------------------------------------------------------------- windows and buttons */
void dsi_window(Surf *s, int x, int y, int w, int h, const char *title, int alpha) {
    hr_shadow(s, x, y, w, h, 32, 16, 10, alpha * 70 / 255);
    hr_fill(s, x, y, w, h, 32, 0xa8a8a8, alpha);
    hr_vgrad(s, x + 4, y + 4, w - 8, h - 8, 28, 0xffffff, 0xf0f0f0, alpha);
    if (title && *title) {
        Accent a = dsi_accent();
        Surf c = *s; surf_clip(&c, x, y, w, 80);
        hr_vgrad(&c, x + 4, y + 4, w - 8, 120, 28, a.light, a.base, alpha);
        rect_vgrad(&c, x + 4, y + 52, w - 8, 28, h_mix(a.light, a.base, 0.7f), a.base, alpha);
        rect_blend(s, x + 4, y + 76, w - 8, 4, a.dark, alpha);
        rect_blend(s, x + 8, y + 8, w - 16, 30, 0xffffff, alpha * 40 / 255);
        char t[160]; ht_fit(HF_BD, title, w - 64, t, sizeof t);
        ht_center(s, HF_BD, x + w / 2 + 3, y + 40 - ht_h(HF_BD) / 2 + 3, a.dark, alpha * 160 / 255, 0, t);
        ht_center(s, HF_BD, x + w / 2, y + 40 - ht_h(HF_BD) / 2, 0xffffff, alpha, 0, t);
    }
}

static void btn_face(Surf *s, int x, int y, int w, int h, int r, uint32_t edge, uint32_t top, uint32_t bot, float press, int alpha) {
    if (press < 0.5f) hr_shadow(s, x, y, w, h, r, 8, (int)(6 - press * 6), alpha * 45 / 255);
    hr_fill(s, x, y, w, h, r, edge, alpha);
    hr_vgrad(s, x + 4, y + 4, w - 8, h - 8, r - 4, top, bot, alpha);
    hr_vgrad(s, x + 6, y + 6, w - 12, (h - 12) / 2, r - 6, 0xffffff, 0xffffff, alpha * (int)(70 - press * 40) / 255);
}
void dsi_face(Surf *s, int x, int y, int w, int h, int r, int focus, float press, int alpha) {
    Accent a = dsi_accent();
    btn_face(s, x, y + (int)(press * 4), w, h, r, focus ? a.base : 0xb8b8b8, press > 0.5f ? 0xe4e4e4 : 0xffffff, focus ? a.pale : 0xe9e9e9, press, alpha);
}
void dsi_face_main(Surf *s, int x, int y, int w, int h, int r, float press, int alpha) {
    Accent a = dsi_accent();
    btn_face(s, x, y + (int)(press * 4), w, h, r, a.dark, press > 0.5f ? a.base : a.light, h_mix(a.base, 0x000000, 0.1f), press, alpha);
}
void dsi_section(Surf *s, int x, int y, int w, const char *text, int alpha) {
    int e = ht(s, HF_XS, x, y, 0x8a8a8a, alpha, 3, text);
    int ly = y + ht_h(HF_XS) / 2 + 2;
    if (x + w - e - 24 > 20) {
        rect_blend(s, e + 20, ly - 2, x + w - e - 20, 3, 0xb4bcc4, alpha * 160 / 255);
        rect_blend(s, e + 20, ly + 1, x + w - e - 20, 3, 0xffffff, alpha * 220 / 255);
    }
}
void dsi_button(Surf *s, int x, int y, int w, int h, const char *label, const char *key, int focus, float press, int alpha) {
    Accent a = dsi_accent();
    y += (int)(press * 4);
    btn_face(s, x, y, w, h, 20, focus ? a.base : 0xb8b8b8, press > 0.5f ? 0xe4e4e4 : 0xffffff, focus ? a.pale : 0xe9e9e9, press, alpha);
    if (focus) hr_ring(s, x + 4, y + 4, w - 8, h - 8, 16, 3, h_mix(a.base, 0xffffff, 0.35f), alpha);
    int kw = key ? 44 + 14 : 0;
    char t[128]; ht_fit(HF_MD, label, w - 32 - kw, t, sizeof t);
    int tw = ht_w(HF_MD, t, 0), x0 = x + (w - tw - kw) / 2;
    if (key) { dsi_glyph(s, x0, y + h / 2 - 22, key, focus ? a.dark : 0x8a8a8a, alpha); x0 += kw; }
    ht(s, HF_MD, x0, y + h / 2 - ht_h(HF_MD) / 2 - 2, focus ? h_mix(a.base, 0x000000, 0.47f) : D_TEXT, alpha, 0, t);
}
void dsi_button_main(Surf *s, int x, int y, int w, int h, const char *label, const char *key, float press, int alpha) {
    Accent a = dsi_accent();
    y += (int)(press * 4);
    btn_face(s, x, y, w, h, 24, a.dark, press > 0.5f ? a.base : a.light, h_mix(a.base, 0x000000, 0.1f), press, alpha);
    int kw = key ? 44 + 14 : 0;
    char t[128]; ht_fit(HF_BD, label, w - 32 - kw, t, sizeof t);
    int tw = ht_w(HF_BD, t, 0), x0 = x + (w - tw - kw) / 2;
    if (key) { dsi_glyph(s, x0, y + h / 2 - 22, key, a.dark, alpha); x0 += kw; }
    int ty = y + h / 2 - ht_h(HF_BD) / 2 - 2;
    ht(s, HF_BD, x0 + 3, ty + 3, a.dark, alpha * 180 / 255, 0, t);
    ht(s, HF_BD, x0, ty, 0xffffff, alpha, 0, t);
}
void dsi_round(Surf *s, float cx, float cy, float r, float press, int alpha) {
    cy += press * 4;
    if (press < 0.5f) h_glow(s, cx, cy + 6, r + 6, 0x000000, alpha * 30 / 255);
    hi_circle(s, cx, cy, r, 0, 0xb4b4b4, alpha);
    hi_circle(s, cx, cy, r - 4, 0, press > 0.5f ? 0xe2e2e2 : 0xf6f6f6, alpha);
    Surf c = *s; surf_clip(&c, (int)(cx - r), (int)(cy - r), (int)(2 * r), (int)r);
    hi_circle(&c, cx, cy, r - 7, 0, 0xffffff, alpha * 140 / 255);
}

/* ---------------------------------------------------------------- the emblem, the logo, the status bar */
static void glyph_ds(Surf *s, float x, float y, uint32_t c, int a) {
    hr_ring(s, (int)x, (int)y + 4, 44, 28, 8, 5, c, a);
    hr_fill(s, (int)x, (int)y + 36, 44, 28, 8, c, a);
}
static void glyph_gba(Surf *s, float x, float y, uint32_t c, int a) {
    hr_fill(s, (int)x, (int)y + 16, 88, 48, 20, c, a);
    hr_fill(s, (int)x + 28, (int)y + 24, 32, 28, 3, 0xffffff, a * 220 / 255);
    rect_blend(s, (int)x + 8, (int)y + 36, 12, 4, 0xffffff, a * 220 / 255); rect_blend(s, (int)x + 12, (int)y + 32, 4, 12, 0xffffff, a * 220 / 255);
    hi_circle(s, x + 74, y + 34, 3, 0, 0xffffff, a * 220 / 255); hi_circle(s, x + 70, y + 42, 3, 0, 0xffffff, a * 220 / 255);
}
void dsi_emblem(Surf *s, int sys, int cy, uint32_t tint, int alpha) {
    static const char *names[] = { "Nintendo DS", "Game Boy Advance", "Favorites", "Recent" };
    const char *name = names[CLAMP(sys, 0, 3)];
    int gw = sys == 0 ? 44 : sys == 1 ? 88 : 52, tw = ht_w(HF_LG, name, 0), w = gw + 24 + tw;
    if (sys == 2) tint = D_GOLD;
    int x = W / 2 - w / 2, y = cy - ht_h(HF_LG) / 2;
    /* pressed into the plastic: a dark edge above, a light one below, the face a shade darker than the stripes */
    uint32_t face = h_mix(0xcdd2d7, tint, 0.18f), edge = h_mix(0x98a1aa, tint, 0.21f);
    for (int pass = 0; pass < 3; pass++) {
        int dy = pass == 0 ? -4 : pass == 1 ? 4 : 0;
        uint32_t c = pass == 0 ? edge : pass == 1 ? 0xffffff : face;
        int a = pass == 1 ? alpha * 240 / 255 : alpha;
        float gy = cy - 32 + dy;
        if (sys == 0) glyph_ds(s, x, gy, c, a);
        else if (sys == 1) glyph_gba(s, x, gy - 8, c, a);
        else if (sys == 2) dsi_star(s, x + 26, cy + dy, 24, pass == 2 ? h_mix(face, D_STAR, 0.6f) : c, a);
        else { hi_circle(s, x + 26, cy + dy, 22, 5, c, a); hi_line(s, x + 26, cy + dy, x + 26, cy - 12 + dy, 5, c, a); hi_line(s, x + 26, cy + dy, x + 36, cy + 6 + dy, 5, c, a); }
        ht(s, HF_LG, x + gw + 24, y + dy, c, a, 0, name);
    }
    int ly = cy + 4;
    for (int side = 0; side < 2; side++) {
        int x0 = side ? x + w + 28 : 48, x1 = side ? W - 48 : x - 28;
        if (x1 - x0 < 24) continue;
        rect_blend(s, x0, ly - 4, x1 - x0, 4, edge, alpha * 150 / 255);
        rect_blend(s, x0, ly, x1 - x0, 4, 0xffffff, alpha * 220 / 255);
        hi_circle(s, side ? x1 + 8.f : x0 - 8.f, ly - 1.f, 6, 0, edge, alpha * 170 / 255);
    }
}

int dsi_logo(const char *name, int max_w, Img *out) {
    /* the words, wrapped (two lines at most), as a coverage mask; then an outline round it, the fill a light
     * gradient, a soft shadow below: the DSi's top-screen title */
    int lh = ht_h(HF_LOGO), pad = 20;
    char l1[160], l2[160]; l1[0] = l2[0] = 0;
    {   /* the break nearest the middle that fits */
        snprintf(l1, sizeof l1, "%s", name);
        if (ht_w(HF_LOGO, name, 0) > max_w) {
            int n = (int)strlen(name), best = -1, bd = 1 << 30;
            for (int i = 1; i < n; i++) if (name[i] == ' ') {
                char a[160], b[160]; snprintf(a, sizeof a, "%.*s", i, name); snprintf(b, sizeof b, "%s", name + i + 1);
                int wa = ht_w(HF_LOGO, a, 0), wb = ht_w(HF_LOGO, b, 0), d = abs(wa - wb) + (MAX(wa, wb) > max_w ? 100000 : 0);
                if (d < bd) { bd = d; best = i; }
            }
            if (best > 0) { snprintf(l1, sizeof l1, "%.*s", best, name); snprintf(l2, sizeof l2, "%s", name + best + 1); }
            char f[160]; ht_fit(HF_LOGO, l1, max_w, f, sizeof f); snprintf(l1, sizeof l1, "%s", f);
            ht_fit(HF_LOGO, l2, max_w, f, sizeof f); snprintf(l2, sizeof l2, "%s", f);
        }
    }
    int lines = l2[0] ? 2 : 1, step = lh - 6;
    int tw = MAX(ht_w(HF_LOGO, l1, 0), l2[0] ? ht_w(HF_LOGO, l2, 0) : 0);
    int mw = tw + 2 * pad, mh = step * (lines - 1) + lh + 2 * pad;
    Surf m; surf_alloc(&m, mw, mh);
    if (!m.px) return -1;
    fill(&m, 0);
    ht_center(&m, HF_LOGO, mw / 2, pad, 0xffffff, 255, 0, l1);
    if (lines == 2) ht_center(&m, HF_LOGO, mw / 2, pad + step, 0xffffff, 255, 0, l2);
    /* the outline: the mask grown by a disc of 7 */
    uint8_t *cov = malloc((size_t)mw * mh), *ol = malloc((size_t)mw * mh);
    if (!cov || !ol) { free(cov); free(ol); surf_free(&m); return -1; }
    for (int i = 0; i < mw * mh; i++) cov[i] = (uint8_t)(m.px[i] & 255);
    const int R = 7;
    uint8_t *tmp = malloc((size_t)mw * mh);
    for (int y = 0; y < mh; y++) for (int x = 0; x < mw; x++) {     /* across */
        int b = 0; for (int i = -R; i <= R; i++) { int xx = x + i; if (xx >= 0 && xx < mw && cov[y * mw + xx] > b) b = cov[y * mw + xx]; }
        tmp[y * mw + x] = (uint8_t)b;
    }
    for (int y = 0; y < mh; y++) for (int x = 0; x < mw; x++) {     /* then down: a square grown (rounded below) */
        int b = 0;
        for (int j = -R; j <= R; j++) { int yy = y + j; if (yy >= 0 && yy < mh && tmp[yy * mw + x] > b) b = tmp[yy * mw + x]; }
        ol[y * mw + x] = (uint8_t)b;
    }
    free(tmp);
    if (img_alloc(out, mw, mh + 12)) { free(cov); free(ol); surf_free(&m); return -1; }
    uint32_t top = 0xf3f7fb, bot = 0x8ea6bc, line = 0x283642;
    for (int y = 0; y < mh + 12; y++) for (int x = 0; x < mw; x++) {
        /* the shadow (the outline, 12 lower, faint), the outline, the fill */
        int sy = y - 12, sa = sy >= 0 && sy < mh ? ol[sy * mw + x] * 50 / 255 : 0;
        int oa = y < mh ? ol[y * mw + x] : 0, fa = y < mh ? cov[y * mw + x] : 0;
        uint32_t c = 0; int a = 0;
        if (sa) { c = 0x000000; a = sa; }
        if (oa) { c = line; a = oa + a * (255 - oa) / 255; }
        if (fa) {
            float t = clampf((float)(y - pad) / (step * (lines - 1) + lh), 0, 1);
            uint32_t f = h_mix(top, bot, t * t);
            c = h_mix(c, f, fa / 255.f);
            a = MAX(a, fa);
        }
        out->px[(size_t)y * mw + x] = a ? (c & 0xffffff) | ((uint32_t)a << 24) : 0;
    }
    free(cov); free(ol); surf_free(&m);
    return 0;
}

static void speaker(Surf *s, int x, int y, uint32_t c, int a) {
    hr_fill(s, x, y + 8, 8, 12, 1, c, a);
    float p[8] = { (float)x + 6, (float)y + 8, (float)x + 18, (float)y - 2, (float)x + 18, (float)y + 30, (float)x + 6, (float)y + 20 };
    hi_poly(s, p, 4, c, a);
    hi_arc(s, x + 18, y + 14, 12, -0.9f, 0.9f, 3.5f, c, a);
}
void dsi_status(Surf *s, int y, int alpha) {
    speaker(s, 16, y + 10, D_TEXT, alpha);
    char date[16], tm[16]; int colon;
    clock_text(date, sizeof date, tm, sizeof tm, &colon);
    if (getenv("DSIXL_FAKE_TIME")) { snprintf(date, sizeof date, "10/24"); snprintf(tm, sizeof tm, "%02d:42", atoi(getenv("DSIXL_FAKE_TIME"))); colon = 1; }
    char hh[3] = { tm[0], tm[1], 0 }, mm[3] = { tm[3], tm[4], 0 };
    int w = ht_w(HF_XS, date, 0) + 20 + ht_w(HF_XS, hh, 0) + 12 + ht_w(HF_XS, mm, 0);
    int x = 932 - w, ty = y + 2;
    x = ht(s, HF_XS, x, ty, D_TEXT, alpha, 0, date) + 20;
    x = ht(s, HF_XS, x, ty, D_TEXT, alpha, 0, hh);
    if (colon) { hr_fill(s, x + 3, ty + 12, 5, 5, 2, D_TEXT, alpha); hr_fill(s, x + 3, ty + 26, 5, 5, 2, D_TEXT, alpha); }
    ht(s, HF_XS, x + 12, ty, D_TEXT, alpha, 0, mm);
    /* the battery: its tip on the left, three blue bars */
    int chg = 0, lv = battery_level(&chg), bx = 952, by = y + 12;
    rect_blend(s, bx, by + 8, 4, 12, D_TEXT, alpha);
    hr_fill(s, bx + 4, by, 48, 28, 3, D_TEXT, alpha);
    hr_fill(s, bx + 8, by + 4, 40, 20, 2, 0xffffff, alpha);
    int bars = lv < 0 ? 3 : lv > 66 ? 3 : lv > 33 ? 2 : lv > 10 ? 1 : 0;
    if (chg) bars = (int)(time(0) % 4);
    uint32_t col = (lv >= 0 && lv <= 10 && !chg) ? 0xe8341c : D_BLUE;
    for (int i = 0; i < bars; i++) rect_vgrad(s, bx + 36 - i * 12, by + 8, 8, 12, h_mix(col, 0xffffff, 0.35f), col, alpha);
}
