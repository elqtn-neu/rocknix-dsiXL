// ui_game.c: a game's page (Y, or Options on the deck). The bottom screen: carry on where you left off, start from
// the title screen, or load one of DraStic's save states, each with both of the DS's screens as they were saved;
// a favourite; this game's own screen look. The top screen shows that look, live, on this game's own pixels (its
// last screens when there's a save state, else its cover).
#define _GNU_SOURCE
#include "ui_int.h"
#include "img.h"
#include "gamecfg.h"
#include "dsi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSLOT 4
enum { F_BACK, F_FAV, F_RESUME, F_START, F_SLOT0, F_LOOK = F_SLOT0 + NSLOT, F_COUNT };

/* the looks: "" is the one Settings sets for every DS game */
static const char *LOOK_V[] = { "", "dsi-lcd", "dsi-lcd-color", "ds-crisp", "ds-crisp-color", "ds-grid", "none" };
static const char *LOOK_N[] = { "As in Settings", "DSi LCD", "DSi LCD + DS colors", "Crisp pixels", "Crisp, DS colors", "Pixel grid", "Smooth" };
static const char *LOOK_D[] = {
    "This game follows the screen look you chose for every DS game.",
    "The DSi screen's colour stripes and its fine gaps between pixels.",
    "The DSi LCD, with the DS screen's own softer, warmer colours.",
    "Sharp square pixels, exactly 4x on this screen.",
    "Sharp pixels in the DS screen's softer colours.",
    "Sharp, with the faint grid of the DS's pixels.",
    "Softly smoothed, the lightest on the battery.",
};
#define NLOOK 7
static const char *GBA_V[] = { "sharp", "lcd", "off" };
static const char *GBA_N[] = { "Sharp pixels", "LCD grid", "Smooth" };
static const char *GBA_D[] = { "Crisp pixels at any size. Every GBA game.", "A handheld's LCD grid. Every GBA game.", "As RetroArch scales it. Every GBA game." };

static Game *G;
static int foc = F_RESUME, look, look_was = -1;
static Spr look_k;                       /* the look's selector (slides as it changes) */
static long long slot_t[NSLOT]; static int slot_on[NSLOT];
static char slot_p[NSLOT][800], res_p[800];
static int has_res; static long long res_t;
static Img preview; static int preview_look = -1; static const void *preview_src; static Game *preview_g;

static const char *global_look(char *b, int n) { bset_get("/data/banana/superdrastic.conf", "DSFLIP_SHADER", b, n, "dsi-lcd"); return b; }
static int nlooks(void) { return G && G->sys == SYS_GBA ? 3 : NLOOK; }

static void load_state(void);
void game_enter(void) {
    G = cur_game(); if (!G) return;
    foc = F_RESUME;
    load_state();
}
static const Img *pixels(void);
static int eff_look(void);
static void preview_make(const Img *src, int lk);
int game_prefetch(Game *g) {
    Game *keep = G; int kf = foc;
    G = g; load_state();
    /* (its saved screens are read off the main thread: not ready yet, try again later) */
    if (g->sys == SYS_NDS && has_res && !art_dss(res_p)) { if (keep && page == P_GAME) { G = keep; foc = kf; load_state(); } return 0; }
    const Img *px = pixels(); int lk = eff_look();
    if (px && (preview_look != lk || preview_src != px || preview_g != G)) { preview_make(px, lk); preview_look = lk; preview_src = px; preview_g = G; }
    cover_prepare(g, 300, 330, 24);
    if (keep && page == P_GAME) { G = keep; foc = kf; load_state(); }
    return 1;
}
static void load_state(void) {
    has_res = resume_available(G, &res_t);
    if (G->sys == SYS_NDS) {
        ds_resume_path(G, res_p, sizeof res_p);
        for (int i = 0; i < NSLOT; i++) { slot_on[i] = ds_slot_path(G, i, slot_p[i], sizeof slot_p[i]); slot_t[i] = slot_on[i] ? file_mtime(slot_p[i]) : 0; }
        char v[64] = "";
        look = 0;
        if (bcfg_get("/data/banana/nds-per-game-shader.conf", G->file, v, sizeof v))
            for (int i = 1; i < NLOOK; i++) if (!strcmp(v, LOOK_V[i])) look = i;
    } else {
        char v[32]; bset_get("/data/banana/settings.conf", "ra_shader", v, sizeof v, "sharp");
        look = 0; for (int i = 0; i < 3; i++) if (!strcmp(v, GBA_V[i])) look = i;
        memset(slot_on, 0, sizeof slot_on);
    }
    look_was = look; spr_set(&look_k, (float)look);
}
static void save_look(void) {
    if (!G || look == look_was) return;
    if (G->sys == SYS_NDS) bset_set("/data/banana/nds-per-game-shader.conf", G->file, look ? LOOK_V[look] : NULL);
    else bset_set("/data/banana/settings.conf", "ra_shader", GBA_V[look]);
    look_was = look;
}

/* ---------------------------------------------------------------- the top screen: the look, live */
/* the game's own pixels: its last top screen (256 x 192) when DraStic saved one, else its cover shrunk to that */
static const Img *pixels(void) {
    static Img fromcover; static Game *fc_g;
    if (G->sys == SYS_NDS && has_res) { const Img *p = art_dss(res_p); if (p) return p; }
    for (int i = 0; i < NSLOT; i++) if (slot_on[i]) { const Img *p = art_dss(slot_p[i]); if (p) return p; }
    Art *A = art_get(G);
    if (fc_g != G || !fromcover.px) {
        img_free(&fromcover);
        if (A && A->state == 1) { hb_fit(&A->cover, &fromcover, 256, 192, 1); fc_g = G; }
    }
    return fromcover.px ? &fromcover : 0;
}
static int eff_look(void) {
    if (G->sys == SYS_GBA) return look == 0 ? 3 : look == 1 ? 5 : 6;   /* (as the DS's: sharp, grid, smooth) */
    if (look) return look;
    char b[64]; global_look(b, sizeof b);
    for (int i = 1; i < NLOOK; i++) if (!strcmp(b, LOOK_V[i])) return i;
    return 1;
}
/* 144 x 108 of the DS's pixels in the middle, at 4x: exactly what the panel would show */
#define PV_W 576
#define PV_H 432
static void preview_make(const Img *src, int lk) {
    img_free(&preview);
    if (img_alloc(&preview, PV_W, PV_H)) return;
    int sx = (256 - 144) / 2, sy = (192 - 108) / 2;
    uint32_t row[PV_W], gap[PV_W], line[144];
    for (int y = 0; y < 108; y++) {
        const uint32_t *s = src->px + (size_t)(sy + y) * src->w + sx;
        for (int x = 0; x < 144; x++) line[x] = s[x] & 0xffffff;
        if (lk == 4 || lk == 2) for (int x = 0; x < 144; x++) line[x] = lcd_nds_color(line[x]);
        int mode = lk == 1 || lk == 2 ? 1 : 0;
        uint32_t *o = preview.px + (size_t)y * 4 * PV_W;
        if (mode && lcd_rows_mode(1, line, 144, 4, row, gap)) {
            for (int j = 0; j < 3; j++) memcpy(o + j * PV_W, row, sizeof row);
            memcpy(o + 3 * PV_W, gap, sizeof gap);
        } else {
            for (int x = 0; x < 144; x++) for (int j = 0; j < 4; j++) row[x * 4 + j] = line[x];
            for (int j = 0; j < 4; j++) memcpy(o + j * PV_W, row, sizeof row);
            if (lk == 5) {   /* the grid: the last row and column of each pixel a little darker */
                for (int x = 0; x < PV_W; x++) { uint32_t c = o[3 * PV_W + x]; o[3 * PV_W + x] = h_mix(c, 0, 0.18f); }
                for (int j = 0; j < 3; j++) for (int x = 3; x < PV_W; x += 4) o[j * PV_W + x] = h_mix(o[j * PV_W + x], 0, 0.12f);
            }
        }
    }
    if (lk == 6) {   /* smooth: the 4x drawn again with a bilinear scale */
        Img small = { 144, 108, 0 }; img_alloc(&small, 144, 108);
        if (small.px) {
            for (int y = 0; y < 108; y++) for (int x = 0; x < 144; x++) small.px[y * 144 + x] = 0xff000000 | (src->px[(size_t)(sy + y) * src->w + sx + x] & 0xffffff);
            Surf t = { PV_W, PV_H, PV_W, preview.px, 0, 0, PV_W, PV_H };
            blit_scaled(&t, &small, 0, 0, PV_W, PV_H, 255, 1);
            img_free(&small);
        }
    }
    for (int i = 0; i < PV_W * PV_H; i++) preview.px[i] |= 0xff000000;
    img_round(&preview, 18);
}
void game_draw_top(Surf *s) {
    surf_noclip(s);
    if (pal.dark) dsi_bg_dark(s, accent, 0.3f, 0); else dsi_bg_top(s, 0, 0);
    if (!G) return;
    uint32_t tx = pal.dark ? 0xdfe3e8 : D_TEXT, sub = pal.dark ? 0x9aa2ad : D_TEXT_SOFT;
    /* the cover as a photo, the name under it */
    hr_shadow(s, 48, 88, 316, 346, 12, 24, 14, pal.dark ? 200 : 90);
    hr_fill(s, 48, 88, 316, 346, 12, 0xffffff, 255);
    hr_ring(s, 48, 88, 316, 346, 12, 2, 0xd0d0d0, 255);
    game_cover(s, G, 58, 98, 296, 326, 4, 255);
    ht_wrap(s, HF_BD, 48, 456, 320, 46, 3, tx, 255, G->title);
    if (G->sub[0]) ht_wrap(s, HF_XS, 48, 456 + 46 * ht_wrap_lines(HF_BD, 320, 3, G->title) + 8, 320, 38, 2, sub, 255, G->sub);
    /* the look, on the game's own pixels, in a DS's screen bezel */
    const Img *px = pixels();
    int lk = eff_look();
    if (px && (preview_look != lk || preview_src != px || preview_g != G)) { preview_make(px, lk); preview_look = lk; preview_src = px; preview_g = G; }
    hr_shadow(s, 392, 76, PV_W + 24, PV_H + 24, 28, 24, 12, pal.dark ? 200 : 100);
    hr_vgrad(s, 392, 76, PV_W + 24, PV_H + 24, 28, 0x55595f, 0x34363b, 255);
    hr_ring(s, 392, 76, PV_W + 24, PV_H + 24, 28, 2, 0x6e7278, 255);
    if (px && preview.px) hb_blit(s, &preview, 404, 88, 255);
    else { hr_fill(s, 404, 88, PV_W, PV_H, 18, 0x0d0f13, 255); ht_center(s, HF_XS, 404 + PV_W / 2, 88 + PV_H / 2 - 16, 0x8a919c, 255, 0, "Its picture is on the way"); }
    const char *nm = G->sys == SYS_GBA ? GBA_N[look] : look ? LOOK_N[look] : LOOK_N[lk];
    ht(s, HF_BD, 404, 548, tx, 255, 0, nm);
    ht_wrap(s, HF_XS, 404, 604, PV_W, 40, 3, sub, 255, G->sys == SYS_GBA ? GBA_D[look] : LOOK_D[look ? look : 0]);
}

/* ---------------------------------------------------------------- the bottom screen */
#define R_Y 128
#define R_H 200
#define R_W 590
#define S_Y 386
#define S_H 176
#define S_W 230
#define L_Y 640
static void rect_of(int f, int *x, int *y, int *w, int *h) {
    switch (f) {
    case F_BACK: *x = 32; *y = 24; *w = 210; *h = 84; return;
    case F_FAV: *x = SW - 32 - 84; *y = 24; *w = 84; *h = 84; return;
    case F_RESUME: *x = 32; *y = R_Y; *w = R_W; *h = R_H; return;
    case F_START: *x = 32 + R_W + 18; *y = R_Y; *w = SW - 64 - R_W - 18; *h = R_H; return;
    case F_LOOK: *x = 32; *y = L_Y; *w = SW - 64; *h = 104; return;
    default: *x = 32 + (f - F_SLOT0) * (S_W + 14); *y = S_Y; *w = S_W; *h = S_H; return;
    }
}
static void sink(int f, int *x, int *y, int *w, int *h, float *p) {
    rect_of(f, x, y, w, h);
    *p = press_of(300 + f); int d = (int)(*p * 5);
    *x += d; *y += d + (int)(*p * 2); *w -= 2 * d; *h -= 2 * d;
}
void game_draw_bot(Surf *s, float yoff, int alpha) {
    int Y = (int)yoff; (void)alpha;
    Surf bg = *s; if (Y > 0) surf_clip(&bg, 0, Y, SW, SH - Y);
    Art *A = G ? art_get(G) : 0;
    uint32_t tint = A && A->state ? A->vivid : accent;
    if (pal.dark) dsi_bg_dark(&bg, tint, 0.3f, 0); else dsi_bg_bottom(&bg, tint, 0.3f, 0);
    if (Y > 0) { rect_blend(s, 0, Y, SW, 6, 0xffffff, 160); rect_blend(s, 0, Y + 6, SW, 3, 0x000000, 30); }
    if (!G) return;
    uint32_t tx = pal.dark ? 0xdfe3e8 : D_TEXT, sub = pal.dark ? 0x9aa2ad : D_TEXT_SOFT;
    int x, y, w, h; float p;
    char a[96], b[64];
    /* back, the name, the star */
    rect_of(F_BACK, &x, &y, &w, &h); p = press_of(300 + F_BACK);
    dsi_button(s, x, y + Y, w, h, "Back", "B", 0, p, 255);
    char t[160]; ht_fit(HF_BD, G->title, SW - 64 - 210 - 84 - 48, t, sizeof t);
    ht(s, HF_BD, 32 + 210 + 24, Y + 66 - ht_h(HF_BD) / 2, tx, 255, 0, t);
    rect_of(F_FAV, &x, &y, &w, &h); p = press_of(300 + F_FAV);
    dsi_round(s, x + w / 2.f, Y + y + h / 2.f, 40, p, 255);
    if (G->fav) dsi_star(s, x + w / 2.f, Y + y + h / 2.f + p * 4, 22, D_STAR, 255);
    else icon(s, IC_STAR_O, x + w / 2.f, Y + y + h / 2.f + p * 4, 40, 0xa0a0a0, 255);
    /* resume (or play): the main button, both of the DS's screens in it */
    rect_of(F_RESUME, &x, &y, &w, &h); y += Y; p = press_of(300 + F_RESUME);
    dsi_face_main(s, x, y, w, h, 28, p, 255);
    y += (int)(p * 4);
    const Img *rp = G->sys == SYS_NDS && has_res ? art_dss(res_p) : 0;
    if (G->sys == SYS_NDS && has_res) dss_screens(s, rp, G, x + 22, y + (h - 172) / 2, 110, 255);
    else { hr_fill(s, x + 20, y + 20, 140, h - 40, 10, 0xffffff, 255); game_cover(s, G, x + 26, y + 26, 128, h - 52, 4, 255); }
    int tx0 = x + 178;
    Accent ac = dsi_accent();
    ht(s, HF_LG, tx0 + 3, y + 40 + 3, ac.dark, 160, 0, has_res ? "Resume" : "Play");
    ht(s, HF_LG, tx0, y + 40, 0xffffff, 255, 0, has_res ? "Resume" : "Play");
    if (has_res) { ago(res_t, b, sizeof b); snprintf(a, sizeof a, "Where you left off, %s", b); }
    else snprintf(a, sizeof a, G->secs > 0 ? "From your save" : "For the first time");
    ht_wrap(s, HF_XS, tx0, y + 40 + ht_h(HF_LG) + 8, x + w - tx0 - 20, 38, 2, 0xffffff, 230, a);
    dsi_glyph(s, x + w - 64, y + 20, "A", ac.dark, 255);
    /* from the start */
    rect_of(F_START, &x, &y, &w, &h); y += Y; p = press_of(300 + F_START);
    dsi_face(s, x, y, w, h, 28, 0, p, 255);
    y += (int)(p * 4);
    icon(s, IC_RESTART, x + 50.f, y + 46.f, 40, ac.base, 255);
    ht(s, HF_BD, x + 26, y + 76, D_TEXT, 255, 0, "From the start");
    ht_wrap(s, HF_XS, x + 26, y + 76 + ht_h(HF_BD), w - 46, 34, 2, D_TEXT_SOFT, 255,
            has_res ? "The title screen. Your save stays." : "The title screen.");
    /* save states: photo frames with both screens in them */
    if (G->sys == SYS_NDS) {
        dsi_section(s, 40, Y + 342, SW - 80, "SAVE STATES", 255);
        for (int i = 0; i < NSLOT; i++) {
            rect_of(F_SLOT0 + i, &x, &y, &w, &h); y += Y; p = press_of(300 + F_SLOT0 + i);
            dsi_face(s, x, y, w, h, 18, 0, slot_on[i] ? p : 0, slot_on[i] ? 255 : 170);
            y += (int)(p * 4);
            snprintf(a, sizeof a, "Slot %d", i + 1);
            int pw = (w - 40) / 2, ph = pw * 3 / 4;
            hr_fill(s, x + 14, y + 14, w - 28, ph + 12, 8, slot_on[i] ? 0x23262c : 0xdcdcdc, 255);
            if (slot_on[i]) {
                const Img *pic = art_dss(slot_p[i]);
                if (pic) {
                    Img up = { 256, 192, pic->px }, lo = { 256, 192, pic->px + 256 * 192 };
                    blit_scaled(s, &up, x + 20.f, y + 20.f, (float)pw, (float)ph, 255, 1);
                    blit_scaled(s, &lo, x + 20.f + pw, y + 20.f, (float)pw, (float)ph, 255, 1);
                }
                ht(s, HF_XS, x + 18, y + h - 74, D_TEXT, 255, 0, a);
                ago(slot_t[i], b, sizeof b);
                char bb[64]; ht_fit(HF_XS, b, w - 32, bb, sizeof bb);
                ht(s, HF_XS, x + 18, y + h - 40, D_TEXT_SOFT, 255, 0, bb);
            } else {
                ht_center(s, HF_XS, x + w / 2, y + 14 + (ph + 12) / 2 - ht_h(HF_XS) / 2, 0xa0a0a0, 255, 0, "Empty");
                ht(s, HF_XS, x + 18, y + h - 56, 0xa0a0a0, 255, 0, a);
            }
        }
    } else {
        dsi_section(s, 40, Y + 342, SW - 80, "THIS GAME", 255);
        dsi_face(s, 32, Y + S_Y, SW - 64, S_H, 18, 0, 0, 255);
        played_text(G, b, sizeof b);
        if (!G->plays) snprintf(a, sizeof a, "Not played yet");
        else if (G->plays == 1) snprintf(a, sizeof a, "Played once, %s in all", b);
        else snprintf(a, sizeof a, "Played %d times, %s in all", G->plays, b);
        ht(s, HF_BD, 60, Y + S_Y + 30, D_TEXT, 255, 0, a);
        ht_wrap(s, HF_XS, 60, Y + S_Y + 88, SW - 120, 38, 2, D_TEXT_SOFT, 255,
                "Save states: RetroArch's own menu, while you play. With Resume on, leaving keeps your place.");
    }
    /* the screen look: < its name > */
    dsi_section(s, 40, Y + 584, SW - 80, G->sys == SYS_NDS ? "SCREEN LOOK  \xc2\xb7  THIS GAME" : "SCREEN LOOK  \xc2\xb7  ALL GBA GAMES", 255);
    rect_of(F_LOOK, &x, &y, &w, &h); y += Y;
    dsi_face(s, x, y, w, h, 24, 0, 0, 255);
    for (int i = 0; i < 2; i++) {
        float q = press_of(400 + i); int bx = i ? x + w - 100 : x + 12;
        dsi_face_main(s, bx, y + 12, 88, h - 24, 20, q, 255);
        float cx = bx + 44.f, cy = y + h / 2.f + q * 4, tri[6];
        if (i) { tri[0] = cx - 10; tri[1] = cy - 16; tri[2] = cx - 10; tri[3] = cy + 16; tri[4] = cx + 14; tri[5] = cy; }
        else { tri[0] = cx + 10; tri[1] = cy - 16; tri[2] = cx + 10; tri[3] = cy + 16; tri[4] = cx - 14; tri[5] = cy; }
        hi_poly(s, tri, 3, 0xffffff, 255);
    }
    int n = nlooks();
    {
        float off = (look_k.x - look) * -220;
        Surf c = *s; surf_clip(&c, x + 110, y, w - 220, h);
        const char *nm = G->sys == SYS_GBA ? GBA_N[look] : LOOK_N[look];
        ht_center(&c, HF_BD, x + w / 2 + (int)off, y + 36 - ht_h(HF_BD) / 2, D_TEXT, 255, 0, nm);
        for (int i = 0; i < n; i++) {
            float cx = x + w / 2.f + (i - (n - 1) / 2.f) * 24;
            hi_circle(s, cx, y + 80.f, i == look ? 7.f : 5.f, 0, i == look ? ac.base : 0xc0c0c0, 255);
        }
    }
    (void)sub; (void)alpha;
}

/* ---------------------------------------------------------------- moving */
static int up_of(int f) {
    if (f == F_RESUME) return F_BACK;
    if (f == F_START) return F_FAV;
    if (f >= F_SLOT0 && f < F_LOOK) return f - F_SLOT0 < 2 ? F_RESUME : F_START;
    if (f == F_LOOK) return G->sys == SYS_NDS ? F_SLOT0 + 1 : F_RESUME;
    return f;
}
static int down_of(int f) {
    if (f == F_BACK) return F_RESUME;
    if (f == F_FAV) return F_START;
    if (f == F_RESUME) return G->sys == SYS_NDS ? F_SLOT0 : F_LOOK;
    if (f == F_START) return G->sys == SYS_NDS ? F_SLOT0 + 2 : F_LOOK;
    if (f >= F_SLOT0 && f < F_LOOK) return F_LOOK;
    return f;
}
static void set_look(int l) {
    int n = nlooks();
    if (l < 0 || l >= n) { sfx(SFX_BUMP); look_k.v += l < 0 ? -6 : 6; dirty(1); return; }
    look = l; look_k.to = (float)l;
    sfx_ex(SFX_TOGGLE, 0.9f + 0.05f * l, 1);
    save_look();
    dirty(0); dirty(1);
}
static void activate(int f) {
    press_hit(300 + f);
    switch (f) {
    case F_BACK: go_page(P_HOME); break;
    case F_FAV:
        library_set_fav(G, !G->fav); sfx_ex(SFX_STAR, G->fav ? 1.0f : 0.8f, 1);
        notice(G->fav ? "Added to your favorites" : "Taken off your favorites");
        home_selection_changed(); dirty(0); dirty(1); break;
    case F_RESUME: start_launch(0); break;
    case F_START: start_launch(1); break;
    case F_LOOK: set_look((look + 1) % nlooks()); break;
    default: {
        int i = f - F_SLOT0;
        if (slot_on[i]) start_launch(2 + i);
        else { sfx(SFX_ERROR); notice("Save a state while you play: DraStic's menu"); }
    }
    }
}
void game_update(float dt) {
    if (!G) { go_page(P_HOME); return; }
    /* a save state's picture arrived (they're read off the main thread): drawn */
    static double poll; static int had = -1;
    if (T - poll > 0.12) {
        poll = T; int n = 0;
        if (G->sys == SYS_NDS && has_res && art_dss(res_p)) n++;
        for (int i = 0; i < NSLOT; i++) if (slot_on[i] && art_dss(slot_p[i])) n++;
        if (n != had) { had = n; dirty(0); dirty(1); }
    }
    if (spr_moving(&look_k, .002f)) { spr_step(&look_k, K_SNAP, C_SNAP - 10, dt); dirty(1); }
    if (pressed(B_B)) { press_hit(300 + F_BACK); go_page(P_HOME); return; }
    if (pressed(B_X)) { activate(F_FAV); return; }
    int was = foc;
    if (repeat(B_UP)) foc = up_of(foc);
    if (repeat(B_DOWN)) foc = down_of(foc);
    if (repeat(B_LEFT)) {
        if (foc == F_LOOK) set_look(look - 1);
        else if (foc == F_START) foc = F_RESUME; else if (foc == F_FAV) foc = F_BACK;
        else if (foc > F_SLOT0 && foc < F_LOOK) foc--;
    }
    if (repeat(B_RIGHT)) {
        if (foc == F_LOOK) set_look(look + 1);
        else if (foc == F_RESUME) foc = F_START; else if (foc == F_BACK) foc = F_FAV;
        else if (foc >= F_SLOT0 && foc < F_LOOK - 1) foc++;
    }
    if (foc != was) sfx(SFX_CURSOR);
    if (pressed(B_A)) { activate(foc); return; }
    if (pressed(B_L1)) set_look(look - 1);
    if (pressed(B_R1)) set_look(look + 1);
    /* the finger */
    int x, y, w, h;
    for (int f = 0; f < F_COUNT; f++) {
        if (G->sys == SYS_GBA && f >= F_SLOT0 && f < F_LOOK) continue;
        rect_of(f, &x, &y, &w, &h);
        if (f == F_LOOK) {
            press_touch(400, x + 8, y + 8, 88, 88); press_touch(401, x + w - 96, y + 8, 88, 88);
            if (tapped(x, y, 110, h)) { foc = f; press_hit(400); set_look(look - 1); return; }
            if (tapped(x + w - 110, y, 110, h)) { foc = f; press_hit(401); set_look(look + 1); return; }
            if (tapped(x + 110, y, w - 220, h)) { foc = f; set_look((look + 1) % nlooks()); return; }
            continue;
        }
        press_touch(300 + f, x, y, w, h);
        if (tapped(x, y, w, h)) { foc = f; activate(f); return; }
    }
    /* a swipe down on the page: back home */
    if (tch_up && tch.id != tch_dead && tch_up_moved && tch.y - tch.y0 > 160 && abs(tch.x - tch.x0) < 120) { go_page(P_HOME); return; }
    rect_of(foc, &x, &y, &w, &h);
    focus_to(x, y, w, h, foc == F_LOOK ? 52 : foc <= F_FAV ? 26 : 30);
}
