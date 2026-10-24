// ui_game.c: a game's page (Y, or Options on the deck). The bottom screen: carry on where you left off, start from
// the title screen, or load one of DraStic's save states, each with both of the DS's screens as they were saved;
// a favourite; this game's own screen look. The top screen shows that look, live, on this game's own pixels (its
// last screens when there's a save state, else its cover).
#define _GNU_SOURCE
#include "ui_int.h"
#include "img.h"
#include "gamecfg.h"
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
void game_prefetch(Game *g) {
    Game *keep = G; int kf = foc;
    G = g; load_state();
    const Img *px = pixels(); int lk = eff_look();
    if (px && (preview_look != lk || preview_src != px || preview_g != G)) { preview_make(px, lk); preview_look = lk; preview_src = px; preview_g = G; }
    cover_prepare(g, 300, 330, 24);
    if (keep && page == P_GAME) { G = keep; foc = kf; load_state(); }
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
    if (!G) { fill(s, pal.bg); return; }
    Art *A = art_get(G);
    uint32_t v = A && A->state ? A->vivid : accent;
    rect_vgrad(s, 0, 0, SW, SH, h_mix(pal.bg, v, pal.dark ? 0.2f : 0.15f), pal.bg, 255);
    hr_shadow(s, 56, 96, 300, 330, 24, 36, 18, pal.dark ? 190 : 100);
    game_cover(s, G, 56, 96, 300, 330, 24, 255);
    ht_wrap(s, HF_LABEL, 56, 446, 300, 36, 3, pal.text, 255, G->title);
    if (G->sub[0]) ht_wrap(s, HF_SMALL, 56, 446 + 36 * ht_wrap_lines(HF_LABEL, 300, 3, G->title) + 6, 300, 28, 2, pal.sub, 255, G->sub);
    /* the look, on its pixels */
    const Img *px = pixels();
    int lk = eff_look();
    if (px && (preview_look != lk || preview_src != px || preview_g != G)) { preview_make(px, lk); preview_look = lk; preview_src = px; preview_g = G; }
    hr_shadow(s, 404, 96, PV_W, PV_H, 18, 36, 18, pal.dark ? 190 : 100);
    if (px && preview.px) hb_blit(s, &preview, 404, 96, 255);
    else { hr_fill(s, 404, 96, PV_W, PV_H, 18, 0x0d0f13, 255); ht_center(s, HF_BODY, 404 + PV_W / 2, 96 + PV_H / 2 - 16, 0x8a919c, 255, 0, "Its picture is on the way"); }
    const char *nm = G->sys == SYS_GBA ? GBA_N[look] : look ? LOOK_N[look] : LOOK_N[lk];
    ht(s, HF_LABEL, 404, 548, pal.text, 255, 0, nm);
    if (G->sys == SYS_NDS && !look) ht(s, HF_SMALL, 404 + ht_w(HF_LABEL, nm, 0) + 14, 554, pal.sub, 255, 0, "(as in Settings)");
    ht_wrap(s, HF_BODY, 404, 594, PV_W, 34, 3, pal.sub, 255, G->sys == SYS_GBA ? GBA_D[look] : LOOK_D[look ? look : 0]);
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
    if (Y > 0) hr_shadow(s, 0, Y, SW, SH, 40, 30, -8, 120);
    hr_fill(s, 0, Y, SW, SH - Y + 60, Y > 0 ? 40 : 0, pal.bg, 255);
    if (Y > 0) hr_fill(s, SW / 2 - 40, Y + 10, 80, 7, 4, pal.track, 255);
    if (!G) return;
    int x, y, w, h; float p;
    char a[96], b[64];
    /* back, the name, the star */
    sink(F_BACK, &x, &y, &w, &h, &p); y += Y;
    card(s, x, y, w, h, 26, (int)(5 - 4 * p));
    icon(s, IC_BACK, x + 38.f, y + h / 2.f, 30, pal.text, 255);
    ht(s, HF_LABEL, x + 62, y + h / 2 - ht_h(HF_LABEL) / 2, pal.text, 255, 0, "Back");
    key_cap(s, x + w - 38, y + h / 2, 18, "B", pal.chip, pal.sub);
    char t[160]; ht_fit(HF_LABEL, G->title, SW - 64 - 210 - 84 - 48, t, sizeof t);
    ht(s, HF_LABEL, 32 + 210 + 24, Y + 66 - ht_h(HF_LABEL) / 2, pal.text, 255, 0, t);
    sink(F_FAV, &x, &y, &w, &h, &p); y += Y;
    card(s, x, y, w, h, 26, (int)(5 - 4 * p));
    icon(s, G->fav ? IC_STAR : IC_STAR_O, x + w / 2.f, y + h / 2.f, 40 * (1 + 0.15f * p), G->fav ? 0xE9A400 : pal.sub, 255);
    /* resume (or play) */
    sink(F_RESUME, &x, &y, &w, &h, &p); y += Y;
    hr_shadow(s, x, y, w, h, 30, 18, 10 - (int)(p * 6), (int)(120 * (1 - p * 0.6f)));
    hr_vgrad(s, x, y, w, h, 30, h_mix(accent_ink, 0xffffff, 0.12f), accent_ink, 255);
    const Img *rp = G->sys == SYS_NDS && has_res ? art_dss(res_p) : 0;
    if (G->sys == SYS_NDS && has_res) dss_screens(s, rp, G, x + 26, y + (h - 172) / 2, 110, 255);
    else game_cover(s, G, x + 24, y + 24, 132, h - 48, 14, 255);
    int tx = x + 180;
    ht(s, HF_TITLE, tx, y + 54, 0xffffff, 255, 0, has_res ? "Resume" : "Play");
    if (has_res) { ago(res_t, b, sizeof b); snprintf(a, sizeof a, "Where you left off, %s", b); }
    else snprintf(a, sizeof a, G->secs > 0 ? "From your save" : "For the first time");
    ht_wrap(s, HF_BODY, tx, y + 54 + ht_h(HF_TITLE) + 6, x + w - tx - 20, 32, 2, 0xffffff, 225, a);
    key_cap(s, x + w - 40, y + 40, 20, "A", h_mix(accent_ink, 0xffffff, 0.26f), 0xffffff);
    /* from the start */
    sink(F_START, &x, &y, &w, &h, &p); y += Y;
    card(s, x, y, w, h, 30, (int)(6 - 5 * p));
    icon(s, IC_RESTART, x + 50.f, y + 52.f, 40, accent_text(), 255);
    ht(s, HF_LABEL, x + 26, y + 92, pal.text, 255, 0, "From the start");
    ht_wrap(s, HF_SMALL, x + 26, y + 92 + ht_h(HF_LABEL) + 4, w - 46, 28, 2, pal.sub, 255,
            has_res ? "The title screen. Your save is kept as it is." : "The title screen.");
    /* save states */
    if (G->sys == SYS_NDS) {
        ht(s, HF_CAPS, 40, Y + 350, pal.sub, 255, 3, "SAVE STATES");
        for (int i = 0; i < NSLOT; i++) {
            sink(F_SLOT0 + i, &x, &y, &w, &h, &p); y += Y;
            card(s, x, y, w, h, 24, slot_on[i] ? (int)(5 - 4 * p) : 0);
            snprintf(a, sizeof a, "Slot %d", i + 1);
            if (slot_on[i]) {
                const Img *pic = art_dss(slot_p[i]);
                /* the two screens side by side (as they were, small) */
                int pw = (w - 36) / 2, ph = pw * 3 / 4;
                hr_fill(s, x + 12, y + 12, w - 24, ph + 12, 12, 0x23262c, 255);
                if (pic) {
                    Img up = { 256, 192, pic->px }, lo = { 256, 192, pic->px + 256 * 192 };
                    blit_scaled(s, &up, x + 18.f, y + 18.f, (float)pw, (float)ph, 255, 1);
                    blit_scaled(s, &lo, x + 18.f + pw, y + 18.f, (float)pw, (float)ph, 255, 1);
                }
                ht(s, HF_BODYB, x + 16, y + h - 54, pal.text, 255, 0, a);
                ago(slot_t[i], b, sizeof b);
                char bb[64]; ht_fit(HF_SMALL, b, w - 32, bb, sizeof bb);
                ht(s, HF_SMALL, x + 16, y + h - 54 + ht_h(HF_BODYB) - 2, pal.sub, 255, 0, bb);
            } else {
                hr_ring(s, x + 12, y + 12, w - 24, h - 70, 12, 3, pal.line, 255);
                ht_center(s, HF_SMALL, x + w / 2, y + 12 + (h - 70) / 2 - ht_h(HF_SMALL) / 2, pal.faint, 255, 0, "Empty");
                ht(s, HF_BODYB, x + 16, y + h - 48, pal.faint, 255, 0, a);
            }
        }
    } else {
        ht(s, HF_CAPS, 40, Y + 350, pal.sub, 255, 3, "THIS GAME");
        card(s, 32, Y + S_Y, SW - 64, S_H, 24, 0);
        played_text(G, b, sizeof b);
        snprintf(a, sizeof a, G->plays ? "Played %d times, %s in all" : "Not played yet", G->plays, b);
        ht(s, HF_LABEL, 60, Y + S_Y + 36, pal.text, 255, 0, a);
        ht_wrap(s, HF_SMALL, 60, Y + S_Y + 84, SW - 120, 28, 2, pal.sub, 255,
                "Save states: in RetroArch's own menu while you play. With Resume on, leaving keeps your place.");
    }
    /* the screen look */
    ht(s, HF_CAPS, 40, Y + 590, pal.sub, 255, 3, G->sys == SYS_NDS ? "SCREEN LOOK  ·  THIS GAME  ·  SEE IT ABOVE" : "SCREEN LOOK  ·  ALL GBA GAMES  ·  SEE IT ABOVE");
    rect_of(F_LOOK, &x, &y, &w, &h); y += Y;
    hr_fill(s, x, y, w, h, 52, pal.track, 255);
    for (int i = 0; i < 2; i++) {
        float q = press_of(400 + i); int bx = i ? x + w - 96 : x + 8, d = (int)(q * 4);
        hr_fill(s, bx + d, y + 8 + d, 88 - 2 * d, 88 - 2 * d, 44, pal.card, 255);
        icon(s, i ? IC_RIGHT : IC_LEFT, bx + 44.f, y + 52.f, 34, pal.sub, 255);
    }
    int n = nlooks();
    {
        /* the name slides with the choice */
        float off = (look_k.x - look) * -220;
        Surf c = *s; surf_clip(&c, x + 110, y, w - 220, h);
        const char *nm = G->sys == SYS_GBA ? GBA_N[look] : LOOK_N[look];
        ht_center(&c, HF_LABEL, x + w / 2 + (int)off, y + 30 - ht_h(HF_LABEL) / 2 + 6, pal.text, 255, 0, nm);
        for (int i = 0; i < n; i++) {
            float cx = x + w / 2.f + (i - (n - 1) / 2.f) * 22;
            hi_circle(s, cx, y + 80.f, i == look ? 6.f : 4.5f, 0, i == look ? accent : pal.faint, 255);
        }
    }
    (void)alpha;
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
    if (tch_up && tch_up_moved && tch.y - tch.y0 > 160 && abs(tch.x - tch.x0) < 120) { go_page(P_HOME); return; }
    rect_of(foc, &x, &y, &w, &h);
    focus_to(x, y, w, h, foc == F_LOOK ? 52 : foc <= F_FAV ? 26 : 30);
}
