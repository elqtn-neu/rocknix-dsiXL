// widgets.c: see widgets.h. Measurements and colours come from the DSi menu at 1:1 (256x192 per screen).
#include "widgets.h"
#include "font.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>

Accent accent(void) { return accent_of(fav_color(cfg.color)); }
Accent accent_of(uint32_t base) {
    Accent a;
    a.base = base;
    a.dark = col_mix(a.base, 0x000000, 80);
    a.mid = col_mix(a.base, 0xffffff, 40);
    a.light = col_mix(a.base, 0xffffff, 120);
    a.pale = col_mix(a.base, 0xffffff, 205);
    return a;
}

/* the pattern: small rounded diamonds on a 16 px lattice (every other row shifted half a cell), drifting a pixel at
 * a time, very faint */
static const uint8_t DIAMOND[7][7] = {
    { 0, 0, 1, 2, 1, 0, 0 }, { 0, 1, 3, 3, 3, 1, 0 }, { 1, 3, 3, 3, 3, 3, 1 }, { 2, 3, 3, 3, 3, 3, 2 },
    { 1, 3, 3, 3, 3, 3, 1 }, { 0, 1, 3, 3, 3, 1, 0 }, { 0, 0, 1, 2, 1, 0, 0 },
};

/* the top screen: the DSi's grey stripes, and the bottom screen's diamonds, fainter and drifting the other way */
void bg_top(Surf *s, double t) {
    for (int y = 0; y < s->h; y++) {
        uint32_t c = (y & 1) ? C_STRIPE_B : C_STRIPE_A, *r = s->px + (size_t)y * s->pitch;
        for (int x = 0; x < s->w; x++) r[x] = c;
    }
    int off = (int)(t * 3) % 32;
    for (int cy = -32; cy < s->h + 32; cy += 16) {
        int row = ((cy + 64) / 16) & 1;
        for (int cx = -32; cx < s->w + 32; cx += 32) {
            int x0 = cx + row * 16 - off, y0 = cy + off / 2;
            for (int j = 0; j < 7; j++) for (int i = 0; i < 7; i++) if (DIAMOND[j][i]) pblend(s, x0 + i, y0 + j, 0xaab2ba, DIAMOND[j][i] * 6);
        }
    }
}
void bg_bottom(Surf *s, double t, uint32_t tint, int amt, float par_x) {
    uint32_t ca = col_mix(0xf2f2f2, col_mix(tint, 0xffffff, 150), amt), cb = col_mix(0xf9f9f9, col_mix(tint, 0xffffff, 190), amt);
    for (int y = 0; y < s->h; y++) {
        uint32_t c = (y & 1) ? cb : ca, *r = s->px + (size_t)y * s->pitch;
        for (int x = 0; x < s->w; x++) r[x] = c;
    }
    uint32_t pc = col_mix(0xb9c4cc, col_mix(tint, 0x606060, 60), amt);
    int off = (int)(t * 5) % 32;          /* 5 px a second along the diagonal */
    int px = ((int)floorf(par_x) % 32 + 32) % 32;     /* and the slide while scrolling: slower than the cartridges */
    for (int cy = -32; cy < s->h + 32; cy += 16) {
        int row = ((cy + 64) / 16) & 1;
        for (int cx = -64; cx < s->w + 32; cx += 32) {
            int x0 = cx + row * 16 + off + px, y0 = cy + off / 2;
            for (int j = 0; j < 7; j++) for (int i = 0; i < 7; i++) if (DIAMOND[j][i]) pblend(s, x0 + i, y0 + j, pc, DIAMOND[j][i] * 7);
        }
    }
}

/* the consoles, as little glyphs drawn in pixels */
static void glyph_ds(Surf *s, int x, int y, uint32_t c, int a) {
    rrect_line(s, x, y + 1, 11, 7, 2, 1.3f, c, a);      /* the top screen: open */
    rrect(s, x, y + 9, 11, 7, 2, c, a);                 /* the bottom screen: solid */
}
static void glyph_gba(Surf *s, int x, int y, uint32_t c, int a) {
    rrect(s, x, y + 4, 22, 12, 5, c, a);                /* the body */
    rect_blend(s, x + 7, y + 6, 8, 7, 0xffffff, a * 220 / 255);   /* the screen */
    rect_blend(s, x + 2, y + 9, 3, 1, 0xffffff, a * 220 / 255); rect_blend(s, x + 3, y + 8, 1, 3, 0xffffff, a * 220 / 255);
    pblend(s, x + 18, y + 8, 0xffffff, a * 220 / 255); pblend(s, x + 17, y + 10, 0xffffff, a * 220 / 255);
}

void emblem(Surf *s, int sys, float cx, int y, uint32_t tint, int alpha) {
    const char *name = sys == 0 ? "Nintendo DS" : "Game Boy Advance";
    int gw = sys == 0 ? 11 : 22, tw = text_w(FONT_LG, name), w = gw + 6 + tw;
    int x = (int)roundf(cx - w / 2.0f);
    /* pressed into the plastic: a dark edge above, a light one below, the face a shade darker than the stripes */
    uint32_t face = col_mix(0xcdd2d7, tint, 45), edge = col_mix(0x98a1aa, tint, 55);
    int gy = y + 5;
    for (int pass = 0; pass < 3; pass++) {
        int dy = pass == 0 ? -1 : pass == 1 ? 1 : 0;
        uint32_t c = pass == 0 ? edge : pass == 1 ? 0xffffff : face;
        int a = pass == 1 ? alpha * 240 / 255 : alpha;
        if (sys == 0) glyph_ds(s, x, gy - 1 + dy, c, a); else glyph_gba(s, x, gy - 3 + dy, c, a);
        text(s, FONT_LG, x + gw + 6, y - 3 + dy, c, a, name);
    }
    /* the thin rule either side, as on a label */
    int ly = y + 13;
    for (int side = 0; side < 2; side++) {
        int x0 = side ? x + w + 7 : 12, x1 = side ? 243 : x - 7;
        if (x1 - x0 < 6) continue;
        hline(s, x0, x1, ly - 1, edge, alpha * 150 / 255);
        hline(s, x0, x1, ly, 0xffffff, alpha * 220 / 255);
        disc(s, side ? x1 + 2.0f : x0 - 2.0f, ly - 0.5f, 1.5f, edge, alpha * 170 / 255);
    }
}

static void speaker_icon(Surf *s, int x, int y, uint32_t c, int a) {
    /* the DSi's volume glyph: a speaker and two waves */
    rect_blend(s, x, y + 3, 2, 4, c, a);
    for (int i = 0; i < 4; i++) vline(s, x + 2 + i, y + 3 - i, y + 6 + i, c, a);
    static const int w1[][2] = { { 8, 2 }, { 9, 3 }, { 9, 4 }, { 9, 5 }, { 9, 6 }, { 8, 7 } };
    static const int w2[][2] = { { 10, 0 }, { 11, 1 }, { 12, 2 }, { 12, 3 }, { 12, 4 }, { 12, 5 }, { 12, 6 }, { 12, 7 }, { 11, 8 }, { 10, 9 } };
    for (int i = 0; i < ARRAY_LEN(w1); i++) pblend(s, x + w1[i][0], y + w1[i][1], c, a);
    for (int i = 0; i < ARRAY_LEN(w2); i++) pblend(s, x + w2[i][0], y + w2[i][1], c, a);
}

static void battery_icon(Surf *s, int x, int y, int alpha, double t) {
    int chg, lvl = battery_level(&chg);
    /* the outline, its tip on the left as on the DSi */
    rect_blend(s, x, y + 2, 2, 4, C_TEXT, alpha);
    rect_blend(s, x + 2, y, 14, 9, C_TEXT, alpha);
    rect_blend(s, x + 3, y + 1, 12, 7, 0xffffff, alpha);
    int bars = lvl < 0 ? 4 : lvl > 75 ? 4 : lvl > 50 ? 3 : lvl > 25 ? 2 : lvl > 10 ? 1 : 0;
    if (chg) bars = ((int)(t * 2.5)) % 5;                   /* charging: the bars fill up in turn */
    uint32_t col = (lvl >= 0 && lvl <= 10 && !chg) ? 0xe8341c : 0x1b8ef2;
    if (!bars && lvl >= 0 && !chg) { if (((int)(t * 2)) & 1) rect_blend(s, x + 4, y + 2, 2, 5, col, alpha); }   /* low: blinks */
    for (int i = 0; i < bars; i++) rect_vgrad(s, x + 12 - i * 3, y + 2, 2, 5, col_mix(col, 0xffffff, 90), col, alpha);
}

void status_bar(Surf *s, float y_off, int alpha, double t) {
    int y = (int)roundf(y_off);
    Accent a = accent();
    speaker_icon(s, 4, y + 3, C_TEXT, alpha);
    (void)a;
    if (is_birthday()) {
        /* a birthday cake after the speaker, its candle flickering */
        int cx = 22, cy = y + 2;
        rect_blend(s, cx, cy + 5, 11, 6, 0xf4c6d6, alpha); rect_blend(s, cx, cy + 5, 11, 2, 0xffffff, alpha);
        rect_blend(s, cx - 1, cy + 11, 13, 1, C_TEXT, alpha);
        for (int i = 0; i < 4; i++) pblend(s, cx + 1 + i * 3, cy + 7, 0xe8476a, alpha);
        rect_blend(s, cx + 5, cy + 1, 1, 4, 0x6aa7e8, alpha);
        pblend(s, cx + 5, cy - (((int)(t * 6)) & 1), 0xffb020, alpha); pblend(s, cx + 5, cy, 0xffe070, alpha);
    }
    char date[16], tm[16]; int colon;
    clock_text(date, sizeof date, tm, sizeof tm, &colon);
    /* "07/29 07 58": the time's colon blinks, drawn by hand between the hours and the minutes */
    int x = 146;
    x = text(s, FONT_MD, x, y - 3, C_TEXT, alpha, date) + 5;
    char hh[3] = { tm[0], tm[1], 0 }, mm[3] = { tm[3], tm[4], 0 };
    x = text(s, FONT_MD, x, y - 3, C_TEXT, alpha, hh);
    if (colon) { rect_blend(s, x + 1, y + 5, 2, 2, C_TEXT, alpha); rect_blend(s, x + 1, y + 9, 2, 2, C_TEXT, alpha); }
    text(s, FONT_MD, x + 4, y - 3, C_TEXT, alpha, mm);
    battery_icon(s, 237, y + 3, alpha, t);
}

void btn_glyph(Surf *s, int x, int y, const char *letter, uint32_t bg, int alpha) {
    rrect(s, x, y, 11, 11, 3.5f, bg, alpha);
    text_center(s, FONT_SM, x + 6, y - 2, 0xffffff, alpha, letter);
}

void tab(Surf *s, int side, int x, int y, int w, const char *label, int active, float press, int alpha) {
    int h = 17;
    float py = y + press * 1.5f;
    uint32_t face_t = active ? 0xffffff : 0xe6e6e6, face_b = active ? 0xf0f0f0 : 0xd6d6d6;
    /* hanging from the top edge: its square corners at the top, rounded at the bottom */
    rrect(s, x + 1, py - 6 + 2, w, h + 6, 6, 0x000000, alpha * 22 / 255);                 /* shadow */
    rrect_vgrad(s, x, py - 6, w, h + 6, 6, face_t, face_b, alpha * (active ? 245 : 200) / 255);
    rrect_line(s, x, py - 6, w, h + 6, 6, 1, active ? 0xb4b4b4 : 0xc6c6c6, alpha);
    Accent a = accent();
    uint32_t tc = active ? C_TEXT : 0x9a9a9a;
    const char *glyph = side ? "R" : "L";
    int tw = text_w(FONT_MD, label);
    if (side == 0) {
        btn_glyph(s, x + 10, (int)py + 3, glyph, active ? 0x3c3c3c : 0x9a9a9a, alpha);
        text(s, FONT_MD, x + 25, (int)py - 2, tc, alpha, label);
    } else {
        text(s, FONT_MD, x + w - 25 - tw, (int)py - 2, tc, alpha, label);
        btn_glyph(s, x + w - 21, (int)py + 3, glyph, active ? 0x3c3c3c : 0x9a9a9a, alpha);
    }
    if (active) rect_blend(s, x + 6, (int)py + h - 2, w - 12, 2, a.base, alpha * 200 / 255);
}

/* the light that passes over the frame now and then: shine 0..1 across it, outside that none */
static void frame_shine(Surf *s, float x, float y, float w, float h, float shine, int alpha) {
    if (shine <= 0 || shine >= 1) return;
    float pos = -h * 0.6f + shine * (w + h * 1.2f);
    int x0 = MAX((int)x, s->cx0), x1 = MIN((int)(x + w), s->cx1), y0 = MAX((int)y, s->cy0), y1 = MIN((int)(y + h), s->cy1);
    for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) {
        float d = (px - x) + (py - y) * 0.6f - pos;
        if (d < -9 || d > 9) continue;
        float cov = rrect_cov(px + 0.5f, py + 0.5f, x + 1, y + 1, w - 2, h - 2, 6);
        if (cov <= 0) continue;
        pblend(s, px, py, 0xffffff, (int)(alpha * cov * 0.45f * expf(-d * d / 14.0f)));
    }
}

void sel_frame(Surf *s, float cx, float cy, float w, float h, const char *label, uint32_t base, float shine, int alpha) {
    Accent a = accent_of(base);
    float x = cx - w / 2, y = cy - h / 2;
    rrect(s, x + 1, y + 2, w, h, 7, 0x000000, alpha * 40 / 255);                          /* shadow */
    rrect(s, x, y, w, h, 7, a.dark, alpha);
    rrect_vgrad(s, x + 1, y + 1, w - 2, h - 2, 6, a.light, col_mix(a.base, 0x000000, 30), alpha);
    rrect_vgrad(s, x + 2, y + 2, w - 4, h * 0.42f, 5, 0xffffff, 0xffffff, alpha * 55 / 255); /* the gloss on the top half */
    rrect_line(s, x + 1, y + 1, w - 2, h - 2, 6, 1, 0xffffff, alpha * 70 / 255);
    frame_shine(s, x, y, w, h, shine, alpha);
    if (label && *label) {
        int ty = (int)(y + h - 17);
        text_center(s, FONT_BD, (int)cx + 1, ty + 1, a.dark, alpha * 200 / 255, label);
        text_center(s, FONT_BD, (int)cx, ty, 0xffffff, alpha, label);
    }
}

#define SB_BTN 19
void scroll_bar(Surf *s, int y, int count, float pos, float view, int press, const uint8_t *marks, int alpha) {
    Accent a = accent();
    int h = 16;
    rect_vgrad(s, 0, y, 256, h, 0xf2f2f2, 0xc6c6c6, alpha);
    hline(s, 0, 255, y, 0xa0a0a0, alpha);
    hline(s, 0, 255, y + 1, 0xffffff, alpha * 160 / 255);
    /* the buttons at the ends */
    for (int side = 0; side < 2; side++) {
        int bx = side ? 256 - SB_BTN : 0, pr = press == (side ? 1 : -1);
        uint32_t t = pr ? a.dark : a.light, b = pr ? a.base : col_mix(a.base, 0x000000, 40);
        rect_vgrad(s, bx, y + 1, SB_BTN, h - 1, t, b, alpha);
        vline(s, side ? bx : bx + SB_BTN - 1, y + 1, y + h - 1, a.dark, alpha);
        float cxx = bx + SB_BTN / 2.0f + (pr ? 0.5f : 0), cyy = y + h / 2.0f + 0.5f + (pr ? 0.5f : 0);
        if (side) tri_fill(s, cxx - 3.5f, cyy - 5, cxx - 3.5f, cyy + 5, cxx + 4.5f, cyy, 0xffffff, alpha);
        else tri_fill(s, cxx + 3.5f, cyy - 5, cxx + 3.5f, cyy + 5, cxx - 4.5f, cyy, 0xffffff, alpha);
    }
    /* the track: one mark per game */
    int tx0 = SB_BTN + 6, tx1 = 256 - SB_BTN - 6, tw = tx1 - tx0, my = y + h / 2;
    for (int x = tx0; x < tx1; x += 4) pblend(s, x, my, 0x9a9a9a, alpha);                 /* the empty dots */
    if (count <= 0) return;
    float step = MIN(6.0f, (float)tw / count);
    float start = tx0 + (tw - step * count) / 2;
    for (int i = 0; i < count; i++) {
        int x = (int)(start + i * step);
        uint32_t mc = marks && marks[i] ? 0xe0a010 : 0x6e766e;     /* the favourites in gold */
        if (step >= 4) rect_blend(s, x, my - 1, 3, 3, mc, alpha);
        else vline(s, x, my - 1, my + 1, mc, alpha);
    }
    int sel = CLAMP((int)roundf(pos), 0, count - 1);
    int sx = (int)(start + sel * step);
    rect_blend(s, sx, my - 1, step >= 4 ? 3 : 1, 3, 0x1b8ef2, alpha);
    /* the window over what's on screen */
    float vw = MAX(14.0f, view * step + 6), vx = start + pos * step + 1.5f - vw / 2;
    vx = clampf(vx, tx0 - 4, tx1 + 4 - vw);
    rrect(s, vx, y + 2, vw, h - 3, 2.5f, 0xffffff, alpha * 150 / 255);
    rrect_line(s, vx, y + 2, vw, h - 3, 2.5f, 1, a.mid, alpha);
    rect_blend(s, sx, my - 1, step >= 4 ? 3 : 1, 3, 0x1b8ef2, alpha);
}

int scroll_bar_hit(int x, int y0, int count, float view, int *zone) {
    (void)y0; (void)view;
    if (x < SB_BTN) { *zone = -1; return -1; }
    if (x >= 256 - SB_BTN) { *zone = 1; return -1; }
    *zone = 0;
    int tx0 = SB_BTN + 6, tx1 = 256 - SB_BTN - 6, tw = tx1 - tx0;
    if (count <= 0) return -1;
    float step = MIN(6.0f, (float)tw / count), start = tx0 + (tw - step * count) / 2;
    return CLAMP((int)((x - start) / step), 0, count - 1);
}

void window(Surf *s, float x, float y, float w, float h, const char *title, int alpha) {
    rrect(s, x + 1, y + 3, w, h, 8, 0x000000, alpha * 45 / 255);
    rrect(s, x, y, w, h, 8, 0xa8a8a8, alpha);
    rrect_vgrad(s, x + 1, y + 1, w - 2, h - 2, 7, 0xffffff, 0xf0f0f0, alpha);
    if (title && *title) {
        Accent a = accent();
        rrect_vgrad(s, x + 1, y + 1, w - 2, 19, 7, a.light, a.base, alpha);
        rect_vgrad(s, (int)x + 1, (int)y + 13, (int)w - 2, 7, col_mix(a.light, a.base, 180), a.base, alpha);
        hline(s, (int)x + 1, (int)(x + w) - 2, (int)y + 20, a.dark, alpha);
        text_center(s, FONT_BD, (int)(x + w / 2) + 1, (int)y + 1, a.dark, alpha * 160 / 255, title);
        text_center(s, FONT_BD, (int)(x + w / 2), (int)y, 0xffffff, alpha, title);
    }
}

void button(Surf *s, float x, float y, float w, float h, const char *label, int focus, float press, int alpha) {
    Accent a = accent();
    y += press;
    if (press < 0.5f) rrect(s, x, y + 1.5f, w, h, 5, 0x000000, alpha * 30 / 255);
    rrect(s, x, y, w, h, 5, focus ? a.base : 0xb8b8b8, alpha);
    rrect_vgrad(s, x + 1, y + 1, w - 2, h - 2, 4, focus ? 0xffffff : 0xffffff, focus ? a.pale : 0xe9e9e9, alpha);
    if (focus) rrect_line(s, x + 1, y + 1, w - 2, h - 2, 4, 1, col_mix(a.base, 0xffffff, 90), alpha);
    if (label) text_center(s, FONT_MD, (int)(x + w / 2), (int)(y + h / 2) - 9, focus ? col_mix(a.base, 0x000000, 120) : C_TEXT, alpha, label);
}

void star(Surf *s, float cx, float cy, float r, uint32_t c, int alpha) {
    if (r < 0.5f || alpha <= 0) return;
    /* five points around a pentagon, the inner radius 0.45 of the outer; an outline one pixel out */
    for (int pass = 0; pass < 2; pass++) {
        float rr = pass ? r : r + 1.3f, ri = rr * 0.45f;
        uint32_t col = pass ? c : 0xffffff;
        float px[10], py[10];
        for (int i = 0; i < 10; i++) {
            float a = -PI_F / 2 + i * PI_F / 5, rad = (i & 1) ? ri : rr;
            px[i] = cx + cosf(a) * rad; py[i] = cy + sinf(a) * rad;
        }
        for (int i = 0; i < 10; i++) tri_fill(s, cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], col, pass ? alpha : alpha * 230 / 255);
    }
    /* a highlight on the upper left */
    pblend(s, (int)(cx - r * 0.25f), (int)(cy - r * 0.3f), 0xffffff, alpha * 150 / 255);
}

void sparkle(Surf *s, float x, float y, float size, uint32_t c, int alpha) {
    int ix = (int)x, iy = (int)y, n = (int)size;
    uint32_t core = col_mix(c, 0xffffff, 170);          /* a white heart, the colour toward the tips */
    for (int i = -n; i <= n; i++) {
        int a = alpha * (n + 1 - abs(i)) / (n + 1);
        uint32_t cc = col_mix(core, c, abs(i) * 256 / (n + 1));
        pblend(s, ix + i, iy, cc, a); if (i) pblend(s, ix, iy + i, cc, a);
    }
    pblend(s, ix - 1, iy - 1, c, alpha / 3); pblend(s, ix + 1, iy - 1, c, alpha / 3);
    pblend(s, ix - 1, iy + 1, c, alpha / 3); pblend(s, ix + 1, iy + 1, c, alpha / 3);
}

void blit_glint(Surf *s, const Img *im, float dx, float dy, float dw, float dh, float phase, float strength) {
    if (!im->px || strength <= 0) return;
    int x0 = MAX((int)floorf(dx), s->cx0), x1 = MIN((int)ceilf(dx + dw), s->cx1);
    int y0 = MAX((int)floorf(dy), s->cy0), y1 = MIN((int)ceilf(dy + dh), s->cy1);
    float kx = im->w / dw, ky = im->h / dh;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float d = ((x - dx) + (y - dy) * 0.6f) - phase;
        if (d < -14 || d > 14) continue;
        int u = (int)((x + 0.5f - dx) * kx), v = (int)((y + 0.5f - dy) * ky);
        if (u < 0 || v < 0 || u >= im->w || v >= im->h) continue;
        int a = (int)CA(im->px[(size_t)v * im->w + u]);
        if (a < 128) continue;
        float band = expf(-d * d / 18.0f) + 0.6f * expf(-(d - 7) * (d - 7) / 3.0f);
        pblend(s, x, y, 0xffffff, (int)(a * band * strength));
    }
}
