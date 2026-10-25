// ui_home.c: the home: the top screen a stage for the selected game (its cover big, its name, how long you've
// played, and where you left off: both of the DS's screens as they were); the bottom screen the deck: the shelves
// (L / R), the cartridges (the d-pad, or a finger: drag, flick, tap), the letters (a finger along them: straight
// there), and what to do (A play, Y its page, X a favourite).
#define _GNU_SOURCE
#include "ui_int.h"
#include "img.h"
#include "cart3d.h"
#include "dsi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* the deck's layout (the DSi's, x4): the tabs hanging from the top edge, the emblem, the game's name, the shelf with
 * the frame round the selected cartridge, the sun and the gear, the scroll bar along the bottom */
/* the tabs: each as wide as what's on it, together centred */
static int tab_w(int i);
static int TABX(int i) {
    int tot = 0; for (int j = 0; j < SH_COUNT; j++) tot += tab_w(j) + (j ? 14 : 0);
    int x = (SW - tot) / 2; for (int j = 0; j < i; j++) x += tab_w(j) + 14;
    return x;
}
#define TABW tab_w(i)
#define TAB_H 68
#define EMB_Y 124
#define NAME_Y 178
#define CAR_Y 380                       /* the selected cartridge's middle */
#define FRAME_DY 34                     /* the frame's middle, below the cartridge's (its START bar) */
#define BTN_Y 640
#define SB_Y (SH - DSB_H)

static Spr sc;                          /* the carousel, in games (the selected one rests at its index) */
static Spr tab_k;                       /* the shelf pill, in tabs */
static Spr lift;                        /* the selected cartridge's lift */
static Spr slide;                       /* a new shelf sliding in (-1 .. 1) */
static Spr pop;                         /* the star's pop on a favourite */
static int dragging, drag_lock, scrub, scrub_letter = -1;
static float drag_base;
static int last_tick;

/* the cartridges' size on the deck: pixels per millimetre (a DS card 35 x 33 mm, a GBA Game Pak 57.5 x 35) */
static float deck_scale(const Game *g) { return g && g->sys == SYS_GBA ? 5.1f : 6.6f; }
static void cart_size(const Game *g, float *w, float *h) {
    float mw, mh, md; cart3d_size(g ? g->sys : SYS_NDS, &mw, &mh, &md);
    *w = mw * deck_scale(g); *h = mh * deck_scale(g);
}
static double gloss_t = -10;            /* the glint sweeping over the selected cartridge's face, once it settles */
static int settle_pending;              /* the shelf is moving to a new selection: its detent when it gets there */

/* the cartridge in a rectangle (the launch's and the return's flights), facing straight on */
void cart_draw(Surf *s, Game *g, float x, float y, float w, float h, int a) {
    float mw, mh, md; cart3d_size(g->sys, &mw, &mh, &md);
    CartPose p; memset(&p, 0, sizeof p);
    p.sx = x + w / 2; p.sy = y + h / 2; p.scale = w / mw; p.alpha = a / 255.f; p.bright = 1; p.pitch = 0.06f;
    cart3d_draw(s, g, &p);
}
void cart_pose_draw(Surf *s, Game *g, const CartPose *p) { cart3d_draw(s, g, p); }
void home_cart_rect(Game *g, float *x, float *y, float *w, float *h) {
    cart_size(g, w, h);
    *x = SW / 2.f - *w / 2; *y = CAR_Y - *h / 2 - 16;
}

/* where the cartridge d places from the middle rests, and how big */
static void cart_place(float d, float *dx, float *k) {
    float a = fabsf(d), sg = d < 0 ? -1.f : 1.f;
    *dx = sg * (a <= 1 ? 300 * a : 300 + (a - 1) * 236);
    *k = 1 - 0.2f * MIN(a, 1) - 0.12f * clampf(a - 1, 0, 1);
}

/* ---------------------------------------------------------------- the letters */
static int letter_of(const Game *g) {
    const char *t = g->title;
    if (!strncasecmp(t, "the ", 4)) t += 4;
    int c = toupper((unsigned char)*t);
    return c >= 'A' && c <= 'Z' ? c - 'A' + 1 : 0;   /* 0: "#" */
}
static int letters_on(void) { return (tab == SH_DS || tab == SH_GBA) && cfg.sort == 0; }

/* ---------------------------------------------------------------- the stage (the top screen), kept as a picture */
static Game *prefetched;
static uint32_t tx_dark_or(uint32_t c) { (void)c; return D_TEXT; }
static int building, build_tab; static Game *build_g;   /* a new stage half made */
static Surf stage, stage_old; static int stage_old_ok;   /* (the old one is a real stage, to fade from) */
static Game *stage_g; static int stage_ok, stage_tab = -1; static double stage_t = -10;
static int stage_wait_dss; static char stage_dss[800];
static double sel_t = -10, sel_gap = 10;          /* when the selection last moved, and the step before that */
void home_selection_changed(void) {
    stage_ok = 0; prefetched = 0; prerender_forget(); dirty(0);
    if (building) build_g = 0;                       /* (a stage half made from what just changed: made again) */
}

/* a small glossy tag (how long it's been played, when) */
static int tag(Surf *s, int x, int y, const char *t, int hot) {
    int w = ht_w(HF_XS, t, 0) + 44;
    hr_shadow(s, x, y, w, 52, 20, 6, 4, 40);
    hr_fill(s, x, y, w, 52, 20, hot ? dsi_accent().base : 0xb8b8b8, 255);
    hr_vgrad(s, x + 4, y + 4, w - 8, 44, 16, 0xffffff, hot ? dsi_accent().pale : 0xececec, 255);
    ht(s, HF_XS, x + 22, y + 26 - ht_h(HF_XS) / 2 - 2, hot ? dsi_accent().dark : D_TEXT, 255, 0, t);
    return x + w + 14;
}
/* a game's name as a logo, kept for the last few */
static const Img *logo_of(Game *g) {
    static struct { Game *g; Img im; unsigned use; } lc[8]; static unsigned clk;
    for (int i = 0; i < 8; i++) if (lc[i].g == g && lc[i].im.px) { lc[i].use = ++clk; return &lc[i].im; }
    int k = 0; for (int i = 1; i < 8; i++) if (lc[i].use < lc[k].use) k = i;
    img_free(&lc[k].im); lc[k].g = g; lc[k].use = ++clk;
    if (dsi_logo(g->title, 520, &lc[k].im)) return 0;
    return &lc[k].im;
}
/* part 1: the backdrop and the cover; 2: the words and the cards; 0: both */
static void stage_paint(Surf *s, Game *g, int part) {
    surf_noclip(s);
    if (part == 2) goto words;
    {
    if (pal.dark) dsi_bg_dark(s, accent, 0.3f, 0); else dsi_bg_top(s, 0, 0);
    if (!g) {
        static const char *what[] = { "No favorites yet", "No DS games yet", "No GBA games yet", "Nothing played yet" };
        static const char *how[] = { "Press X on a game, and it waits for you here.", "Put them in /data/roms/nds on the card.",
                                     "Put them in /data/roms/gba on the card.", "The games you play come here, the newest first." };
        ht_center(s, HF_LG, SW / 2, 300, pal.dark ? 0xd8dce2 : D_TEXT, 255, 0, what[tab]);
        ht_center(s, HF_MD, SW / 2, 390, pal.dark ? 0x9aa2ad : D_TEXT_SOFT, 255, 0, how[tab]);
        stage_wait_dss = 0;
        return;
    }
    /* the cover as a photo: a white border, a soft shadow under it, a floor shadow */
    ellipse_soft(s, 244, 560, 200, 26, 0x000000, pal.dark ? 90 : 55);
    hr_shadow(s, 52, 92, 384, 420, 14, 28, 16, pal.dark ? 200 : 90);
    hr_fill(s, 52, 92, 384, 420, 14, 0xffffff, 255);
    hr_ring(s, 52, 92, 384, 420, 14, 2, 0xd0d0d0, 255);
    game_cover(s, g, 64, 104, 360, 396, 6, 255);
    if (part == 1) return;
    }
words:
    if (!g) return;
    uint32_t tx = pal.dark ? 0xdfe3e8 : D_TEXT, sub = pal.dark ? 0x9aa2ad : D_TEXT_SOFT;
    int x = 476, y = 100;
    /* what it is, then its name as the DSi writes it on its top screen */
    int e = ht(s, HF_XS, x + 4, y, sub, 255, 4, g->sys == SYS_GBA ? "GAME BOY ADVANCE" : "NINTENDO DS");
    if (g->fav) dsi_star(s, (float)e + 26, (float)y + 18, 16, D_STAR, 255);
    const Img *lg = logo_of(g);
    if (lg) { hb_blit(s, lg, x - 16, y + 36, 255); y += 36 + lg->h - 8; }
    else y += 120;
    char a[64], b[64];
    if (g->secs >= 60 || g->last_played) {
        played_text(g, b, sizeof b); snprintf(a, sizeof a, "Played %s", b);
        int cx = tag(s, x, y, a, 0);
        if (g->last_played) { ago(g->last_played, b, sizeof b); tag(s, cx, y, b, 0); }
    } else tag(s, x, y, "Not played yet", 1);
    /* where you left off: both of the DS's screens as they were, in a window */
    long long when = 0;
    stage_wait_dss = 0;
    if (resume_available(g, &when)) {
        int wy = MAX(y + 76, 392), wh = SH - 28 - wy, ww = 500;
        dsi_window(s, x, wy, ww, wh, "Continue", 255);
        const Img *pic = 0;
        if (g->sys == SYS_NDS) {
            ds_resume_path(g, stage_dss, sizeof stage_dss);
            pic = art_dss(stage_dss);
            stage_wait_dss = !pic;
        }
        int sh = wh - 104, swid = MIN(170, (sh - 20) * 2 / 3);
        if (g->sys == SYS_NDS) dss_screens(s, pic, g, x + 24, wy + 92, swid, 255);
        else game_cover(s, g, x + 24, wy + 96, swid, sh - 8, 8, 255);
        int tx0 = x + 24 + swid + 36, tw = ww - (tx0 - x) - 20;
        ht_wrap(s, HF_BD, tx0, wy + 110, tw, 44, 2, tx_dark_or(tx), 255, "Where you left off");
        ago(when, b, sizeof b); snprintf(a, sizeof a, "Saved %s", b);
        ht_wrap(s, HF_XS, tx0, wy + 206, tw, 38, 2, sub, 255, a);
        ht(s, HF_XS, tx0, wy + wh - 64, dsi_accent().base, 255, 0, "A  Resume");
    } else {
        int wy = MAX(y + 76, 420);
        dsi_window(s, x, wy, 500, 200, 0, 255);
        ht_wrap(s, HF_XS, x + 32, wy + 30, 436, 40, 4, D_TEXT_SOFT, 255,
                g->sys == SYS_NDS ? "No resume point yet. Leave a game with Resume on, and you'll pick up right here."
                                  : "No resume point yet. RetroArch's own auto save state brings you back here.");
    }
}
void home_draw_top(Surf *s) {
    if (!stage.px) { surf_alloc(&stage, SW, SH); surf_alloc(&stage_old, SW, SH); }
    Game *g = cur_game();
    int fresh = g != stage_g || stage_tab != tab;
    if (fresh && stage_g && sel_gap < 0.16 && T - sel_t < 0.09 && page == P_HOME && seq == Q_NONE) {
        /* moving fast through the shelf: the stage waits until it stops (each step would cost a whole new stage) */
        dirty(0);
    } else if (fresh && page == P_HOME && seq == Q_NONE && !(building && build_g == g && build_tab == tab)) {
        /* a new game: its stage made over two frames (the backdrop and cover, then the rest), the old one shown
         * meanwhile, so neither frame keeps the deck waiting; then the crossfade */
        if (!building) { Surf t = stage_old; stage_old = stage; stage = t; stage_old_ok = stage_g != 0; }
        building = 1; build_g = g; build_tab = tab;
        stage_paint(&stage, g, 1);
        surf_copy(s, &stage_old);
        dirty(0);
        return;
    } else if (!stage_ok || fresh) {
        if (building && build_g == g && build_tab == tab) { stage_paint(&stage, g, 2); stage_t = T; }
        else {
            if (fresh && !building) { Surf t = stage_old; stage_old = stage; stage = t; stage_t = T; stage_old_ok = stage_g != 0; }
            stage_paint(&stage, g, 0);
        }
        building = 0;
        stage_g = g; stage_tab = tab; stage_ok = 1;
    }
    surf_copy(s, &stage);
    float k = (float)((T - stage_t) / 0.16);
    if (k < 1 && seq == Q_NONE && stage_old.px && stage_old_ok) {
        /* the last one fades out over it: quick, so moving through the shelf keeps up */
        uint32_t a = (uint32_t)(256 * (1 - ease_out_cubic(k))), ia = 256 - a;
        for (int y = 0; y < SH; y++) {
            uint32_t *d = s->px + (size_t)y * s->pitch; const uint32_t *o = stage_old.px + (size_t)y * stage_old.pitch;
            for (int x = 0; x < SW; x++) {
                uint32_t c = o[x], e = d[x];
                d[x] = ((((c & 0xff00ff) * a + (e & 0xff00ff) * ia) >> 8) & 0xff00ff) | ((((c & 0x00ff00) * a + (e & 0x00ff00) * ia) >> 8) & 0x00ff00);
            }
        }
        dirty(0);
    }
}

/* ---------------------------------------------------------------- the deck (the bottom screen) */
static const char *TABS[SH_COUNT] = { "*", "DS", "GBA", "Recent" };
static int tab_w(int i) {
    static const char *L[SH_COUNT] = { "", "DS", "GBA", "Recent" };
    int w = 56 + (i == 0 ? 44 : ht_w(HF_BD, L[i], 0)) + (i ? 54 : 0) + (i == 0 || i == SH_COUNT - 1 ? 60 : 0);
    return w;
}
typedef struct { Spr wob; } Fx;         /* a cartridge's jelly: it wobbles as it lands, and settles */
static Fx fxs[SH_COUNT][MAXG];
static Fx *fx_of(int i) { return i >= 0 && i < MAXG ? &fxs[tab][i] : 0; }
static Spr frame_k;                     /* the frame's pop when the selection locks in */
/* a little balloon over a cartridge ("Resume", "New") */
static void dsi_balloon_small(Surf *s, float x, float y, const char *t, int a) {
    int w = ht_w(HF_XS, t, 0) + 36, h = 44;
    int bx = (int)x - 10, by = (int)y - h;
    uint32_t edge = !strcmp(t, "New") ? 0xd8452a : dsi_accent().dark, top = !strcmp(t, "New") ? 0xff8a6a : dsi_accent().light, bot = !strcmp(t, "New") ? 0xe5484d : dsi_accent().base;
    hr_shadow(s, bx, by, w, h, 16, 6, 3, a * 60 / 255);
    hr_fill(s, bx, by, w, h, 16, edge, a);
    hr_vgrad(s, bx + 3, by + 3, w - 6, h - 6, 13, top, bot, a);
    float tri[6] = { bx + 18.f, by + h - 2.f, bx + 32.f, by + h - 2.f, bx + 20.f, by + h + 12.f };
    hi_poly(s, tri, 3, edge, a);
    ht(s, HF_XS, bx + 18, by + h / 2 - ht_h(HF_XS) / 2 - 2, 0xffffff, a, 0, t);
}
static Spr tint_k;                      /* how much the deck takes on the selected game's colour */
static uint32_t tint_c = 0x8899aa;
static float frame_shine(void) { float k = (float)(T - gloss_t) / 0.9f; return k > 0 && k < 1 ? k : -1; }
/* the frame round the selected cartridge: its size for that cartridge */
static void frame_size(const Game *g, float *w, float *h) { float cw, ch; cart_size(g, &cw, &ch); *w = cw + 76; *h = ch + 140; }

void home_draw_bot(Surf *s, float yoff, int alpha) {
    int Y = (int)yoff; (void)alpha;
    Game *g = cur_game();
    int n = shn[tab];
    /* the background: the DSi's stripes and diamonds (cream with stars on the favourites), toward the game's colour */
    float par = -sc.x * 44 - slide.x * 300;
    Surf b = *s;
    if (Y > 0) { surf_clip(&b, 0, Y, SW, SH - Y); }
    if (pal.dark) dsi_bg_dark(&b, tint_c, tint_k.x, par);
    else if (tab == SH_FAV) dsi_bg_favs(&b, tint_c, tint_k.x, par);
    else dsi_bg_bottom(&b, tint_c, tint_k.x, par);
    if (Y > 0) { rect_blend(s, 0, Y, SW, 6, 0xffffff, 120); }
    /* the shelves: the tabs hanging from the top edge (L on the first, R on the last) */
    for (int i = 0; i < SH_COUNT; i++) {
        int act = i == tab;
        dsi_tab(s, TABX(i), Y, TABW, i == 0 ? "L" : i == SH_COUNT - 1 ? "R" : 0, i == SH_COUNT - 1, TABS[i], i ? shn[i] : -1,
                act, press_of(120 + i) + (act ? 0 : 0.0f), 255);
    }
    /* the emblem, pressed into the plastic, and the selected game's name under it */
    dsi_emblem(s, tab == SH_FAV ? 2 : tab == SH_RECENT ? 3 : tab == SH_GBA ? 1 : 0, Y + EMB_Y, tint_c, 255);
    uint32_t tx = pal.dark ? 0xdfe3e8 : D_TEXT;
    if (g) {
        char t[160]; ht_fit(HF_MD, g->title, 800, t, sizeof t);
        int tw = ht_w(HF_MD, t, 0) + (g->fav ? 52 : 0), x0 = SW / 2 - tw / 2;
        if (g->fav) { dsi_star(s, x0 + 18, Y + NAME_Y + ht_h(HF_MD) / 2.f, 16, D_STAR, 255); x0 += 52; }
        ht(s, HF_MD, x0, Y + NAME_Y, tx, 255, 0, t);
    }
    if (!n) {
        static const char *what[] = { "Your favorites go here", "No DS games on the card", "No GBA games on the card", "Nothing played yet" };
        ht_center(s, HF_BD, SW / 2, Y + 360, D_TEXT_SOFT, 255, 0, what[tab]);
    } else {
        float ox = slide.x * 520;
        int sa = (int)(255 * (1 - MIN(1.f, fabsf(slide.x) * 1.4f)));
        /* the frame round the selected one: it goes after the cartridge coming to the middle, lunging and settling */
        if (g && !(seq == Q_LAUNCH)) {
            float fw, fh; frame_size(g, &fw, &fh);
            float fx = SW / 2 + ox + (sc.to - sc.x) * -40;
            dsi_frame(s, fx, Y + CAR_Y + FRAME_DY - 16 * lift.x * 0.2f, fw + frame_k.x * 14, fh + frame_k.x * 10, "START", frame_shine(), press_of(130), sa);
        }
        int list[9], nl = 0;
        for (int d = 3; d >= 0; d--) {
            int a = (int)roundf(sc.x) - d, b2 = (int)roundf(sc.x) + d;
            if (a >= 0 && a < n) list[nl++] = a;
            if (d && b2 >= 0 && b2 < n) list[nl++] = b2;
        }
        for (int q = 0; q < nl; q++) {
            int i = list[q];
            Game *c = shelf[tab][i];
            float d = i - sc.x, dx, k; cart_place(d, &dx, &k);
            float lf = i == sel[tab] ? lift.x : 0;
            float cx = SW / 2 + dx + ox, cy = Y + CAR_Y - 14 * lf;
            int a = (int)(sa * (1 - clampf(fabsf(d) - 1.8f, 0, 1)));
            if (a <= 0) continue;
            if (i == sel[tab] && (seq == Q_LAUNCH || (seq == Q_RETURN && seq_t < 1.05))) continue;   /* (it's in flight) */
            CartPose pz; memset(&pz, 0, sizeof pz);
            pz.sx = cx; pz.sy = cy; pz.scale = deck_scale(c) * k * (1 + 0.04f * lf);
            /* turned toward the middle (a shelf you look along), leaning with the shelf's speed */
            pz.yaw = -clampf(d, -1.4f, 1.4f) * 0.62f - clampf(sc.v, -12, 12) * 0.018f;
            pz.pitch = 0.08f - 0.08f * lf; pz.roll = clampf(sc.v, -12, 12) * -0.004f;
            pz.alpha = a / 255.f; pz.bright = 1.0f - 0.10f * clampf(fabsf(d), 0, 1.5f);
            pz.desat = 0.30f * clampf(fabsf(d), 0, 1);
            pz.reflect = i == sel[tab] ? 0 : 0.22f;
            Fx *f = fx_of(i);
            float wob = f ? f->wob.x : 0;
            pz.stretch_y = 1 + 0.07f * wob; pz.stretch_x = 1 - 0.05f * wob;
            if (i == sel[tab]) {
                float pr = press_of(130);
                pz.stretch_y -= 0.05f * pr; pz.stretch_x += 0.03f * pr;
                float gk = (float)(T - gloss_t) / 0.7f;
                if (gk >= 0 && gk < 1) { pz.gloss = sinf(gk * PI_F) * 0.9f; pz.gloss_pos = lerpf(-0.4f, 2.2f, gk); dirty(1); }
            }
            cart3d_draw(s, c, &pz);
            float cw, chh; cart_size(c, &cw, &chh);
            float w = cw * k, h = chh * k;
            int fresh = c->is_new && !c->last_played;
            if ((c->resume || fresh) && fabsf(d) < 1.5f) dsi_balloon_small(s, cx - w / 2 + 6, cy - h / 2 - 6, c->resume ? "Resume" : "New", a);
            if (c->fav && tab != SH_FAV) {
                float z = 22 * k * (i == sel[tab] ? 1 + 0.5f * sinf(pop.x * PI_F) : 1);
                dsi_star(s, cx + w / 2 - 6, cy - h / 2 + 4, z, D_STAR, a);
            }
        }
    }
    /* the sun (brightness) and the gear (settings), and the game's options */
    {
        float p0 = press_of(110), p1 = press_of(111);
        dsi_round(s, 76, Y + BTN_Y, 44, p1, 255);
        icon(s, IC_SUN, 76, Y + BTN_Y + p1 * 4 - 4, 42, 0xe8a010, 255);
        for (int i = 0; i < 5; i++) hi_circle(s, 76 - 20 + i * 10.f, Y + BTN_Y + 26.f + p1 * 4, 3.5f, 0, i < bri_lv ? 0xe8a010 : 0xc8c8c8, 255);
        dsi_round(s, SW - 76, Y + BTN_Y, 44, p0, 255);
        icon(s, IC_GEAR, SW - 76, Y + BTN_Y + p0 * 4, 46, 0x7a7a7a, 255);
        if (g) dsi_button(s, SW - 140 - 264, Y + BTN_Y - 34, 264, 68, "Options", "Y", 0, press_of(112), 255);
        if (g) dsi_button(s, 140, Y + BTN_Y - 34, 264, 68, g->fav ? "Unstar" : "Favorite", "X", 0, press_of(114), 255);
    }
    /* the scroll bar along the bottom: a mark for each game, the favourites in gold */
    {
        static uint8_t marks[MAXG];
        for (int i = 0; i < n; i++) marks[i] = (uint8_t)(shelf[tab][i]->fav != 0);
        dsi_scroll_bar(s, Y + SB_Y, n, sc.x, press_of(140) > 0.3f ? -1 : press_of(141) > 0.3f ? 1 : 0, marks, 255);
    }
    /* a finger along the scroll bar: the letter it's at, big, over it */
    if (scrub && scrub_letter >= 0) {
        float bx = clampf((float)tch.x, 90, SW - 90), by = Y + SB_Y - 100.f;
        hr_shadow(s, (int)bx - 64, (int)by - 64, 128, 128, 64, 20, 8, 120);
        hi_circle(s, bx, by, 64, 0, dsi_accent().dark, 255);
        hi_circle(s, bx, by, 58, 0, dsi_accent().base, 255);
        char l[4] = { scrub_letter ? (char)('A' + scrub_letter - 1) : '#', 0 };
        ht_center(s, HF_LG, (int)bx, (int)by - ht_h(HF_LG) / 2, 0xffffff, 255, 0, l);
    }
}

static void select_to(int i, int quiet) {
    int n = shn[tab];
    if (!n) return;
    i = CLAMP(i, 0, n - 1);
    if (i == sel[tab]) return;
    int far = abs(i - sel[tab]) > 1;
    sel[tab] = i;
    sel_gap = T - sel_t; sel_t = T; settle_pending = 1;
    { Fx *f = fx_of(i); if (f) f->wob.v -= 6; }
    if (!dragging) sc.to = (float)i;
    lift.x = 0; lift.v = 0; lift.to = 1;
    if (!quiet) sfx_ex(SFX_CURSOR, far ? 1.1f : 0.96f + 0.08f * (float)((i * 7) % 5) / 4, 1);
    if (tab == SH_DS || tab == SH_GBA) snprintf(cfg.last[shelf[tab][i]->sys], sizeof cfg.last[0], "%s", shelf[tab][i]->file);
    if (tab == SH_FAV) snprintf(cfg.last_fav, sizeof cfg.last_fav, "%s", shelf[tab][i]->file);
    home_selection_changed(); dirty(1);
}
static void set_tab(int t) {
    t = (t + SH_COUNT) % SH_COUNT;
    if (t == tab) return;
    int dir = (t > tab) ? 1 : -1;
    if (tab == SH_COUNT - 1 && t == 0) dir = 1;
    if (tab == 0 && t == SH_COUNT - 1) dir = -1;
    tab = t; cfg.sys = t;
    tab_k.to = (float)t;
    spr_set(&sc, (float)sel[t]);
    slide.x = (float)dir * 0.6f; slide.v = 0; slide.to = 0;
    lift.x = 0; lift.to = 1;
    sfx(SFX_TAB);
    home_selection_changed(); dirty(1);
}
static void toggle_fav(Game *g) {
    library_set_fav(g, !g->fav);
    sfx_ex(SFX_STAR, g->fav ? 1.0f : 0.8f, 1); buzz(20, 0.35f);
    pop.x = 0; pop.v = 0; pop.to = g->fav ? 1 : 0;
    if (g->fav) { pop.x = 0; pop.to = 1; } else { pop.x = 0; pop.to = 0; }
    Game *keep = g;
    shelves_build();
    if (tab == SH_FAV) {   /* (taken off the favorites shelf: the next one moves in) */
        for (int i = 0; i < shn[tab]; i++) if (shelf[tab][i] == keep) sel[tab] = i;
        sc.to = (float)sel[tab];
    }
    notice(g->fav ? "Added to your favorites" : "Taken off your favorites");
    home_selection_changed(); dirty(1);
}

void home_enter(void) {
    static int first = 1;
    Game *was = cur_game();
    shelves_build();
    if (first) {   /* (the menu's start: which games have a resume point; a game page changes none) */
        for (int s = 0; s < SYS_COUNT; s++) for (int i = 0; i < lib[s].n; i++) lib[s].g[i].resume = resume_available(&lib[s].g[i], 0);
        first = 0;
    }
    spr_set(&sc, (float)sel[tab]); spr_set(&tab_k, (float)tab); spr_set(&lift, 1); spr_set(&slide, 0);
    dragging = scrub = 0;
    if (cur_game() != was) home_selection_changed();
    dirty(1);
}

static void motion(float dt) {
    /* the jelly, the frame's pop, the deck's colour easing toward the selected game's */
    for (int i = MAX(0, sel[tab] - 4); i <= MIN(shn[tab] - 1, sel[tab] + 4); i++) {
        Spr *w = &fxs[tab][i].wob;
        if (spr_moving(w, .002f)) { spr_step(w, 430, 9, dt); dirty(1); }
    }
    if (spr_moving(&frame_k, .002f)) { spr_step(&frame_k, 520, 16, dt); dirty(1); }
    {
        Game *g = cur_game(); Art *A = g ? art_get(g) : 0;
        uint32_t want = A && A->state ? A->vivid : accent;
        if (want != tint_c) {
            float k = MIN(1.f, dt * 6);
            uint32_t n = h_mix(tint_c, want, k);
            if (n == tint_c) n = want;
            tint_c = n; dirty(1);
        }
        tint_k.to = 0.38f;
        if (spr_moving(&tint_k, .002f)) { spr_step(&tint_k, K_SOFT, C_SOFT, dt); dirty(1); }
    }
    /* the detent: the selection has locked into place: a click you feel, a small hop, a glint across its face */
    if (settle_pending && !dragging && !spr_moving(&sc, .03f) && fabsf(sc.x - sel[tab]) < 0.04f) {
        settle_pending = 0;
        sfx_ex(SFX_NOTCH, 1.0f, 0.55f); buzz(10, 0.25f);
        lift.v -= 3.5f; gloss_t = T + 0.04; frame_k.v -= 3.2f;
        Fx *f = fx_of(sel[tab]); if (f) f->wob.v += 5;
        dirty(1);
    }
    if (spr_moving(&sc, .001f) && !dragging) {
        int before = (int)roundf(sc.x);
        spr_step(&sc, 300, 30, dt);
        int after = (int)roundf(sc.x);
        (void)before; (void)after;
        dirty(1);
    }
    if (spr_moving(&tab_k, .001f)) { spr_step(&tab_k, K_SNAP, C_SNAP - 8, dt); dirty(1); }
    if (spr_moving(&lift, .002f)) { spr_step(&lift, K_BOUNCE, C_BOUNCE - 6, dt); dirty(1); }
    if (spr_moving(&slide, .002f)) { spr_step(&slide, K_SNAP, C_SNAP, dt); dirty(1); }
    if (pop.to > 0 || pop.x > 0) {
        if (pop.to > 0) { pop.x += dt * 3.2f; if (pop.x >= 1) { pop.x = 0; pop.to = 0; } dirty(1); }
        else pop.x = 0;
    }
    if (stage_wait_dss && art_dss(stage_dss)) home_selection_changed();
}

/* the cartridges next to the selection made while nothing moves (one a frame), so moving to them never waits */
static void prewarm(void) {
    /* (only on frames where nothing moves: then its cost is never seen) */
    if (spr_moving(&sc, .01f) || spr_moving(&lift, .01f) || spr_moving(&slide, .01f) || spr_moving(&tab_k, .01f) || dragging || scrub ||
        building || T - stage_t < 0.2 || T - sel_t < 0.15 || tch.down || in.held || frame_dirty() || !shn[tab]) return;
    for (int d = 1; d <= 4; d++) for (int sg = -1; sg <= 1; sg += 2) {
        int i = sel[tab] + sg * d;
        if (i < 0 || i >= shn[tab]) continue;
        Art *A = art_get(shelf[tab][i]);
        if (A && A->state != 0 && cart3d_prepare(shelf[tab][i])) return;
    }
    /* then the covers the stage will want next (the neighbours'), so a step to them costs no resampling */
    for (int d = 0; d <= 2; d++) for (int sg = -1; sg <= 1; sg += 2) {
        int i = sel[tab] + sg * d;
        if (i < 0 || i >= shn[tab]) continue;
        Art *A = art_get(shelf[tab][i]);
        if (A && A->state == 1 && !((A->hero.px && A->hero.w == 360 && A->hero.h == 396) || (A->hero2.px && A->hero2.w == 360 && A->hero2.h == 396))) {
            cover_prepare(shelf[tab][i], 360, 396, 26); return;
        }
    }
    /* then the selected game's page, so Y opens it at once */
    Game *g = cur_game();
    if (g && g != prefetched && T - sel_t > 0.25 && page == P_HOME && seq == Q_NONE) {
        Art *A = art_get(g);
        if (A && A->state != 0 && game_prefetch(g)) { prefetched = g; prerender_game(g); }
    }
}
void home_update(float dt) {
    if (dt < 0) { motion(-dt); return; }
    motion(dt);
    prewarm();
    focus_hide();
    int n = shn[tab];
    Game *g = cur_game();
    /* the buttons */
    if (repeat(B_LEFT)) { press_hit(140); if (sel[tab] > 0) select_to(sel[tab] - 1, 0); else if (pressed(B_LEFT)) { sfx(SFX_BUMP); buzz2(14, 0.3f); sc.v -= 4; } }
    if (repeat(B_RIGHT)) { press_hit(141); if (sel[tab] < n - 1) select_to(sel[tab] + 1, 0); else if (pressed(B_RIGHT)) { sfx(SFX_BUMP); buzz2(14, 0.3f); sc.v += 4; } }
    if (repeat(B_UP) && letters_on() && g) {           /* up / down: a letter at a time */
        int l = letter_of(g);
        for (int i = sel[tab] - 1; i >= 0; i--) if (letter_of(shelf[tab][i]) != l) { int l2 = letter_of(shelf[tab][i]); while (i > 0 && letter_of(shelf[tab][i - 1]) == l2) i--; select_to(i, 0); break; }
    }
    if (repeat(B_DOWN) && letters_on() && g) {
        int l = letter_of(g);
        for (int i = sel[tab] + 1; i < n; i++) if (letter_of(shelf[tab][i]) != l) { select_to(i, 0); break; }
    }
    if (pressed(B_L1)) { press_hit(120 + (tab + SH_COUNT - 1) % SH_COUNT); set_tab(tab - 1); }
    if (pressed(B_R1)) { press_hit(120 + (tab + 1) % SH_COUNT); set_tab(tab + 1); }
    if (g && pressed(B_A)) { press_hit(130); start_launch(0); return; }
    if (g && pressed(B_Y)) { press_hit(112); go_page(P_GAME); return; }
    if (g && pressed(B_X)) { press_hit(114); toggle_fav(g); return; }
    if (pressed(B_SELECT)) { press_hit(110); go_page(P_SETTINGS); return; }

    /* the finger */
    for (int i = 0; i < SH_COUNT; i++) press_touch(120 + i, TABX(i), 0, TABW, TAB_H);
    press_touch(111, 76 - 48, BTN_Y - 48, 96, 96);
    press_touch(110, SW - 76 - 48, BTN_Y - 48, 96, 96);
    press_touch(112, SW - 140 - 264, BTN_Y - 34, 264, 68);
    press_touch(114, 140, BTN_Y - 34, 264, 68);
    press_touch(140, 0, SB_Y, DSB_BTN, DSB_H);
    press_touch(141, SW - DSB_BTN, SB_Y, DSB_BTN, DSB_H);

    /* the frame: a tap starts the game; a finger held there sinks it, and at half a second the game's page opens */
    float fw = 0, fh = 0; if (g) frame_size(g, &fw, &fh);
    int fx0 = SW / 2 - (int)fw / 2, fy0 = CAR_Y + FRAME_DY - (int)fh / 2;
    if (n && g) {
        press_touch(130, fx0, fy0, (int)fw, (int)fh);
        if (tch.down && !tch.moved && tch.id != drag_lock && tch.id != tch_dead && T - tch.t0 > 0.5 &&
            tch.x0 >= fx0 && tch.x0 < fx0 + fw && tch.y0 >= fy0 && tch.y0 < fy0 + fh) {
            drag_lock = tch.id; buzz(25, 0.4f); go_page(P_GAME); return;
        }
    }
    if (tch.down && tch.id != drag_lock && tch.id != tch_dead) {
        /* where did it go down: the scroll bar's track (scrubbing), or the shelf (dragging) */
        int zone; dsi_scroll_hit(tch.x0, n, &zone);
        if (tch.y0 >= SB_Y && zone == 0 && n) { scrub = 1; drag_lock = tch.id; scrub_letter = -2; }
        else if (tch.y0 >= 200 && tch.y0 < 580 && n && tch.moved) {
            if (abs(tch.x - tch.x0) >= abs(tch.y - tch.y0)) { dragging = 1; drag_base = sc.x; drag_lock = tch.id; }
            else if (tch.y - tch.y0 < -40 && abs(tch.x0 - SW / 2) < 160) {
                /* a flick up from the middle cartridge: in it goes */
                drag_lock = tch.id; start_launch(0); return;
            }
        }
    }
    if (scrub) {
        if (tch.down) {
            int z, i = dsi_scroll_hit(tch.x, n, &z);
            if (i >= 0 && i != sel[tab]) { select_to(i, 1); sfx_ex(SFX_NOTCH, 1.0f + i * 0.004f, 0.6f); buzz(5, 0.15f); }
            int l = i >= 0 && letters_on() ? letter_of(shelf[tab][i]) : -1;
            if (l != scrub_letter) { scrub_letter = l; dirty(1); }
        } else { scrub = 0; scrub_letter = -1; dirty(1); }
        return;
    }
    if (dragging) {
        if (tch.down) {
            float x = drag_base - (tch.x - tch.x0) / 300.f;
            if (x < 0) x = x * 0.35f;                         /* (a pull past the end gives a little, then stops) */
            if (x > n - 1) x = (n - 1) + (x - (n - 1)) * 0.35f;
            sc.x = x; sc.v = 0;
            int at = CLAMP((int)roundf(x), 0, n - 1);
            if (at != last_tick) { last_tick = at; sfx_ex(SFX_NOTCH, 1.1f, 0.5f); buzz(6, 0.2f); Fx *f = fx_of(at); if (f) f->wob.v -= 3; }
            if (at != sel[tab]) { sel[tab] = at; lift.x = 0.4f; lift.to = 1; sel_gap = T - sel_t; sel_t = T; settle_pending = 1; home_selection_changed(); }
            dirty(1);
        } else {
            dragging = 0;
            /* a flick carries on: as far as its speed takes it */
            float v = -tch.vx / 300.f, to = sc.x + v * 0.22f;
            int i = CLAMP((int)roundf(to), 0, n - 1);
            sc.v = v; sc.to = (float)i;
            int was = sel[tab]; sel[tab] = -1; select_to(i, i == was); if (sel[tab] < 0) sel[tab] = i;
            sc.to = (float)i;
        }
        return;
    }
    if (!tch_up || tch_up_moved || tch.id == drag_lock) return;
    for (int i = 0; i < SH_COUNT; i++) if (tapped(TABX(i), 0, TABW, TAB_H)) { set_tab(i); return; }
    if (tapped(0, SB_Y, DSB_BTN, DSB_H)) { if (sel[tab] > 0) select_to(sel[tab] - 1, 0); else { sfx(SFX_BUMP); sc.v -= 4; } return; }
    if (tapped(SW - DSB_BTN, SB_Y, DSB_BTN, DSB_H)) { if (sel[tab] < n - 1) select_to(sel[tab] + 1, 0); else { sfx(SFX_BUMP); sc.v += 4; } return; }
    if (tapped(SW - 76 - 48, BTN_Y - 48, 96, 96)) { go_page(P_SETTINGS); return; }
    if (tapped(76 - 48, BTN_Y - 48, 96, 96)) { brightness_set(bri_lv % 5 + 1); sfx_ex(SFX_BRIGHT, 0.85f + bri_lv * 0.07f, 1); dirty(1); return; }
    if (g && tapped(SW - 140 - 264, BTN_Y - 34, 264, 68)) { go_page(P_GAME); return; }
    if (g && tapped(140, BTN_Y - 34, 264, 68)) { toggle_fav(g); return; }
    if (g && tapped(fx0, fy0, (int)fw, (int)fh)) { start_launch(0); return; }
    /* a cartridge to the side: it comes to the middle */
    if (n && tch.y0 >= 220 && tch.y0 < 560) {
        float best = 1e9; int bi = -1;
        for (int i = MAX(0, sel[tab] - 2); i <= MIN(n - 1, sel[tab] + 2); i++) {
            if (i == sel[tab]) continue;
            float dx, k; cart_place(i - sc.x, &dx, &k);
            float cw, ch; cart_size(shelf[tab][i], &cw, &ch);
            float d = fabsf(tch.x0 - (SW / 2 + dx));
            if (d < cw * k / 2 && d < best) { best = d; bi = i; }
        }
        if (bi >= 0) select_to(bi, 0);
    }
}
