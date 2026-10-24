// ui_home.c: the home: the top screen a stage for the selected game (its cover big, its name, how long you've
// played, and where you left off: both of the DS's screens as they were); the bottom screen the deck: the shelves
// (L / R), the cartridges (the d-pad, or a finger: drag, flick, tap), the letters (a finger along them: straight
// there), and what to do (A play, Y its page, X a favourite).
#define _GNU_SOURCE
#include "ui_int.h"
#include "img.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define M 28                            /* the margin round a cartridge's sprite (its baked shadow) */
#define CAR_Y 296                       /* the selected cartridge's middle, on the deck */
#define TAB_X 104
#define TAB_W 816
#define LET_X 48
#define LET_W 928
#define LET_Y 590
#define BAR_Y 640

static Spr sc;                          /* the carousel, in games (the selected one rests at its index) */
static Spr tab_k;                       /* the shelf pill, in tabs */
static Spr lift;                        /* the selected cartridge's lift */
static Spr slide;                       /* a new shelf sliding in (-1 .. 1) */
static Spr pop;                         /* the star's pop on a favourite */
static int dragging, drag_lock, scrub, scrub_letter = -1, built;
static float drag_base;
static int last_tick;

static void cart_size(const Game *g, float *w, float *h) {
    if (g && g->sys == SYS_GBA) { *w = 324; *h = 202; } else { *w = 248; *h = 236; }
}

/* ---------------------------------------------------------------- the cartridges */
typedef struct { Game *g; int w, h; Img label; } CartCtx;
static void cart_paint(Surf *s, void *v) {
    CartCtx *c = v; int w = c->w, h = c->h;
    hr_shadow(s, M, M, w, h, 18, 22, 12, 100);
    if (c->g->sys == SYS_GBA) {
        hr_vgrad(s, M, M, w, h, 18, 0xb3b6be, 0x878a93, 255);
        hr_fill(s, M + 2, M + 26, w - 4, 3, 1, 0x7d8089, 120);                   /* the step under its top */
        for (int i = 0; i < 5; i++) hr_fill(s, M + w / 2 - 60 + i * 26, M + 9, 16, 8, 3, 0xffffff, 60);
        if (c->label.px) hb_blit(s, &c->label, M + 28, M + 40, 255);
        hr_ring(s, M + 26, M + 38, w - 52, h - 56, 9, 2, 0x6e717a, 160);
    } else {
        hr_vgrad(s, M, M, w, h, 18, 0x535863, 0x2f3238, 255);
        hr_fill(s, M + w - 40, M, 40, 20, 8, 0x2a2d32, 255);                      /* the corner notch */
        for (int i = 0; i < (w - 80) / 12; i++) hr_fill(s, M + 22 + i * 12, M + 12, 6, 10, 2, 0xffffff, 40);
        if (c->label.px) hb_blit(s, &c->label, M + 16, M + 34, 255);
        hr_ring(s, M + 15, M + 33, w - 30, h - 48, 11, 2, 0x000000, 90);
    }
    hr_ring(s, M, M, w, h, 18, 2, 0xffffff, 26);                                  /* a catch of light on the edge */
}
static void label_make(Game *g, int w, int h, int r, Img *out) {
    Art *A = art_get(g);
    if (A && A->state == 1 && A->cover.px) { hb_fit(&A->cover, out, w, h, 1); img_round(out, r); return; }
    /* none: one from its colours, its icon, its name */
    Surf t; surf_alloc(&t, w, h);
    if (!t.px) return;
    uint32_t v = A && A->state ? A->vivid : 0x8590a6, d = A && A->state ? A->deep : 0x3b4352;
    rect_vgrad(&t, 0, 0, w, h, v, d, 255);
    int horiz = g->sys == SYS_GBA;
    if (g->icon.px) hb_scaled(&t, &g->icon, horiz ? w - 96.f - 12 : (w - 96) / 2.f, horiz ? (h - 96) / 2.f : 22.f, 96, 96, 255);
    ht_wrap(&t, HF_SMALL, 14, horiz ? h / 2 - ht_h(HF_SMALL) : h - 14 - 2 * ht_h(HF_SMALL), horiz ? w - 130 : w - 28, ht_h(HF_SMALL), 2, 0xffffff, 240, g->title);
    if (!img_alloc(out, w, h)) for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) out->px[(size_t)y * w + x] = 0xff000000 | t.px[(size_t)y * t.pitch + x];
    surf_free(&t);
    img_round(out, r);
}
static Img *cart_sprite(Game *g, int force) {
    Art *A = art_get(g);
    if (!A) return 0;
    if (A->sprite.px) return &A->sprite;
    if (A->state == 0 && !force) return 0;           /* (its art is on the way: wait for it, a moment) */
    if (!force && built >= 1) return 0;               /* (at most one a frame: scrolling never stalls) */
    built++;
    float fw, fh; cart_size(g, &fw, &fh);
    CartCtx c = { g, (int)fw, (int)fh, { 0 } };
    if (g->sys == SYS_GBA) label_make(g, c.w - 56, c.h - 60, 8, &c.label);
    else label_make(g, c.w - 32, c.h - 50, 10, &c.label);
    hb_capture(&A->sprite, c.w + 2 * M, c.h + 2 * M, cart_paint, &c);
    img_free(&c.label);
    return A->sprite.px ? &A->sprite : 0;
}
void cart_draw(Surf *s, Game *g, float x, float y, float w, float h, int a) {
    float cw, ch; cart_size(g, &cw, &ch);
    float k = w / cw, kh = h / ch;
    Img *sp = cart_sprite(g, 1);
    if (sp) hb_scaled(s, sp, x - M * k, y - M * kh, sp->w * k, sp->h * kh, a);
}
static void cart_ghost(Surf *s, Game *g, float x, float y, float w, float h, int a) {
    /* (a cartridge whose sprite isn't made yet: its shape and an empty label, for a frame or two) */
    int gba = g->sys == SYS_GBA;
    float k = w / (gba ? 324.f : 248.f);
    hr_fill(s, (int)x, (int)y, (int)w, (int)h, (int)(18 * k), gba ? 0x9da0a8 : 0x41454d, a);
    hr_fill(s, (int)(x + (gba ? 28 : 16) * k), (int)(y + (gba ? 40 : 34) * k), (int)(w - (gba ? 56 : 32) * k), (int)(h - (gba ? 60 : 50) * k),
            (int)(10 * k), gba ? 0x868991 : 0x5a5f69, a);
}
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
static Surf stage, stage_old;
static Game *stage_g; static int stage_ok, stage_tab = -1; static double stage_t = -10;
static int stage_wait_dss; static char stage_dss[800];
static double sel_t = -10, sel_gap = 10;          /* when the selection last moved, and the step before that */
void home_selection_changed(void) { stage_ok = 0; prefetched = 0; dirty(0); }

static int chip(Surf *s, int x, int y, const char *t, uint32_t bg, uint32_t fg) {
    int w = ht_w(HF_BODYB, t, 0) + 36;
    hr_fill(s, x, y, w, 46, 23, bg, 255);
    ht(s, HF_BODYB, x + 18, y + 23 - ht_h(HF_BODYB) / 2, fg, 255, 0, t);
    return x + w + 12;
}
static void stage_paint(Surf *s, Game *g) {
    surf_noclip(s);
    Art *A = g ? art_get(g) : 0;
    uint32_t v = A && A->state ? A->vivid : accent;
    rect_vgrad(s, 0, 0, SW, SH, h_mix(pal.bg, v, pal.dark ? 0.22f : 0.18f), pal.bg, 255);
    if (!g) {
        static const char *what[] = { "No favorites yet", "No DS games yet", "No GBA games yet", "Nothing played yet" };
        static const char *how[] = { "Press X on a game, and it waits for you here.", "Put them in /data/roms/nds on the card.",
                                     "Put them in /data/roms/gba on the card.", "The games you play come here, the newest first." };
        ht_center(s, HF_HERO, SW / 2, 300, pal.text, 255, 0, what[tab]);
        ht_center(s, HF_BODY, SW / 2, 380, pal.sub, 255, 0, how[tab]);
        stage_wait_dss = 0;
        return;
    }
    hr_shadow(s, 64, 104, 360, 396, 26, 44, 24, pal.dark ? 200 : 110);
    game_cover(s, g, 64, 104, 360, 396, 26, 255);
    /* what it is */
    int x = 472, y = 112;
    int e = ht(s, HF_CAPS, x, y, accent_text(), 255, 3, g->sys == SYS_GBA ? "GAME BOY ADVANCE" : "NINTENDO DS");
    if (g->fav) icon(s, IC_STAR, (float)e + 22, (float)y + 12, 28, 0xE9A400, 255);
    int n = ht_wrap(s, HF_HERO, x, y + 34, 500, 60, 3, pal.text, 255, g->title);
    y += 34 + n * 60 + 16;
    char a[64], b[64];
    if (g->secs >= 60 || g->last_played) {
        played_text(g, b, sizeof b); snprintf(a, sizeof a, "Played %s", b);
        int cx = chip(s, x, y, a, pal.chip, pal.text);
        if (g->last_played) { ago(g->last_played, b, sizeof b); chip(s, cx, y, b, pal.chip, pal.text); }
    } else chip(s, x, y, "Not played yet", h_mix(accent, pal.bg, 0.8f), accent_text());
    /* where you left off */
    long long when = 0;
    stage_wait_dss = 0;
    if (resume_available(g, &when)) {
        int cy = MAX(y + 70, 400), ch = 312;
        if (cy + ch > SH - 40) cy = SH - 40 - ch;
        card(s, x, cy, 488, ch, 28, 6);
        const Img *pic = 0;
        if (g->sys == SYS_NDS) {
            ds_resume_path(g, stage_dss, sizeof stage_dss);
            pic = art_dss(stage_dss);
            stage_wait_dss = !pic;
        }
        if (g->sys == SYS_NDS) dss_screens(s, pic, g, x + 26, cy + (ch - 272) / 2, 168, 255);
        else game_cover(s, g, x + 26, cy + 26, 180, ch - 52, 14, 255);
        int tx = x + 26 + 190 + 18;
        ht(s, HF_CAPS, tx, cy + 64, accent_text(), 255, 3, "CONTINUE");
        int ln = ht_wrap(s, HF_LABEL, tx, cy + 96, 488 - (tx - x) - 20, 36, 2, pal.text, 255, "Where you left off");
        ago(when, b, sizeof b); snprintf(a, sizeof a, "Saved %s", b);
        ht_wrap(s, HF_BODY, tx, cy + 108 + ln * 36, 488 - (tx - x) - 20, 32, 2, pal.sub, 255, a);
    } else {
        int cy = MAX(y + 70, 420);
        card(s, x, cy, 488, 150, 28, 4);
        ht_wrap(s, HF_BODY, x + 32, cy + 38, 424, 34, 3, pal.sub, 255,
                g->sys == SYS_NDS ? "No resume point yet. Leave a game with the menu's Resume on, and you'll pick up right here."
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
    } else if (!stage_ok || fresh) {
        if (fresh && stage_g != (Game *)-1) { Surf t = stage_old; stage_old = stage; stage = t; stage_t = T; }
        stage_paint(&stage, g);
        stage_g = g; stage_tab = tab; stage_ok = 1;
    }
    surf_copy(s, &stage);
    float k = (float)((T - stage_t) / 0.16);
    if (k < 1) {
        /* the last one fades out over it: quick, so moving through the shelf keeps up */
        int a = (int)(256 * (1 - ease_out_cubic(k)));
        for (int y = 0; y < SH; y++) {
            uint32_t *d = s->px + (size_t)y * s->pitch, *o = stage_old.px + (size_t)y * stage_old.pitch;
            for (int x = 0; x < SW; x++) {
                uint32_t c = o[x], e = d[x];
                if (c == e) continue;
                uint32_t rb = (((c & 0xff00ff) * (uint32_t)a + (e & 0xff00ff) * (uint32_t)(256 - a)) >> 8) & 0xff00ff;
                uint32_t gg = (((c & 0x00ff00) * (uint32_t)a + (e & 0x00ff00) * (uint32_t)(256 - a)) >> 8) & 0x00ff00;
                d[x] = rb | gg;
            }
        }
        dirty(0);
    }
}

/* ---------------------------------------------------------------- the deck (the bottom screen) */
static const char *TABS[SH_COUNT] = { "Favorites", "DS", "GBA", "Recent" };
static int tab_x(int i) { return TAB_X + 7 + i * ((TAB_W - 14) / SH_COUNT); }
#define TAB_SW ((TAB_W - 14) / SH_COUNT - 6)

static int play_w(void) { return ht_w(HF_BTN, "Resume", 0) + 58 + 76; }
static const char *play_label(Game *g) { return g && resume_available(g, 0) ? "Resume" : "Play"; }

void home_draw_bot(Surf *s, float yoff, int alpha) {
    int Y = (int)yoff;
    built = 0;
    hr_fill(s, 0, Y, SW, SH - Y + 60, Y > 0 ? 40 : 0, pal.bg, 255);
    /* the shelves */
    hr_fill(s, 32, Y + 47, 58, 46, 12, pal.chip, 255); ht_center(s, HF_BODYB, 61, Y + 70 - ht_h(HF_BODYB) / 2, pal.sub, 255, 0, "L");
    hr_fill(s, SW - 90, Y + 47, 58, 46, 12, pal.chip, 255); ht_center(s, HF_BODYB, SW - 61, Y + 70 - ht_h(HF_BODYB) / 2, pal.sub, 255, 0, "R");
    hr_fill(s, TAB_X, Y + 24, TAB_W, 92, 46, pal.track, 255);
    float px = TAB_X + 7 + tab_k.x * ((TAB_W - 14) / SH_COUNT);
    hr_shadow(s, (int)px, Y + 31, TAB_SW, 78, 39, 12, 4, pal.shadow_a);
    hr_fill(s, (int)px, Y + 31, TAB_SW, 78, 39, pal.card, 255);
    for (int i = 0; i < SH_COUNT; i++) {
        float near = 1 - clampf(fabsf(tab_k.x - i), 0, 1);
        uint32_t c = h_mix(pal.sub, pal.text, near);
        char cnt[12]; snprintf(cnt, sizeof cnt, "%d", shn[i]);
        int lw = ht_w(HF_LABEL, TABS[i], 0), cw = ht_w(HF_SMALL, cnt, 0), cx = tab_x(i) + TAB_SW / 2 - (lw + 10 + cw) / 2;
        float p = press_of(120 + i);
        int ty = Y + 70 - ht_h(HF_LABEL) / 2 + (int)(p * 3);
        ht(s, HF_LABEL, cx, ty, c, 255, 0, TABS[i]);
        ht(s, HF_SMALL, cx + lw + 10, ty + 5, near > .5f ? accent_text() : pal.faint, 255, 0, cnt);
    }
    Game *g = cur_game();
    /* the cartridges */
    int n = shn[tab];
    if (!n) {
        static const char *what[] = { "Your favorites go here", "No DS games on the card", "No GBA games on the card", "Nothing played yet" };
        ht_center(s, HF_TITLE, SW / 2, Y + 260, pal.sub, 255, 0, what[tab]);
    } else {
        float ox = slide.x * 520;
        int sa = (int)(255 * (1 - MIN(1.f, fabsf(slide.x) * 1.4f)));
        float fw, fh; cart_size(g, &fw, &fh);
        ellipse_soft(s, SW / 2 + ox, Y + CAR_Y + fh / 2 + 22, 210 - 30 * lift.x, 22, 0x000000, (int)((pal.dark ? 110 : 60) * (1 - .3f * lift.x)) * sa / 255);
        int list[9], nl = 0;
        /* (by distance, far first) */
        for (int d = 3; d >= 0; d--) {
            int a = (int)roundf(sc.x) - d, b = (int)roundf(sc.x) + d;
            if (a >= 0 && a < n) list[nl++] = a;
            if (d && b >= 0 && b < n) list[nl++] = b;
        }
        for (int q = 0; q < nl; q++) {
            int i = list[q];
            Game *c = shelf[tab][i];
            float d = i - sc.x, dx, k; cart_place(d, &dx, &k);
            float cw, chh; cart_size(c, &cw, &chh);
            float lf = i == sel[tab] ? lift.x - press_of(130) * 0.9f : 0;
            float w = cw * k * (1 + 0.04f * lf), h = chh * k * (1 + 0.04f * lf);
            float cx = SW / 2 + dx + ox, cy = Y + CAR_Y - 16 * lf;
            int a = (int)(sa * (1 - clampf(fabsf(d) - 1.8f, 0, 1)));
            if (a <= 0) continue;
            if (i == sel[tab] && (seq == Q_LAUNCH || (seq == Q_RETURN && seq_t < 1.05))) continue;   /* (it's in flight) */
            Img *sp = cart_sprite(c, 0);
            if (sp) hb_scaled(s, sp, cx - w / 2 - M * k, cy - h / 2 - M * k, sp->w * w / cw, sp->h * h / chh, a);
            else cart_ghost(s, c, cx - w / 2, cy - h / 2, w, h, a);
            int fresh = c->is_new && !c->last_played;
            if ((c->resume || fresh) && fabsf(d) < 1.5f) {
                const char *b = c->resume ? "RESUME" : "NEW";
                uint32_t bc = c->resume ? accent_ink : 0xE5484D;
                int bw = ht_w(HF_CAPS, b, 2) + 28, bx = (int)(cx - w / 2) - 8, by = (int)(cy - h / 2) - 12;
                hr_shadow(s, bx, by, bw, 38, 19, 8, 3, 90);
                hr_fill(s, bx, by, bw, 38, 19, bc, a);
                ht(s, HF_CAPS, bx + 14, by + 19 - ht_h(HF_CAPS) / 2, 0xffffff, a, 2, b);
            }
            if (c->fav && tab != SH_FAV) {
                float z = 34 * k * (i == sel[tab] ? 1 + 0.5f * sinf(pop.x * PI_F) : 1);
                float sx = cx + w / 2 - 6, sy = cy - h / 2 + 6;
                hi_circle(s, sx, sy, z * 0.72f, 0, 0xffffff, a);
                icon(s, IC_STAR, sx, sy, z, 0xE9A400, a);
            }
        }
        /* the arrows */
        for (int i = 0; i < 2; i++) {
            float p = press_of(101 + i);
            int bx = i ? SW - 92 : 20, d = (int)(p * 4);
            card(s, bx + d, Y + 256 + d, 72 - 2 * d, 104 - 2 * d, 22, 3);
            int on = i ? sel[tab] < n - 1 : sel[tab] > 0;
            icon(s, i ? IC_RIGHT : IC_LEFT, bx + 36.f, Y + 308.f, 34, on ? pal.sub : pal.faint, 255);
        }
        /* its name */
        if (g) {
            char t[160]; ht_fit(HF_TITLE, g->title, 780, t, sizeof t);
            ht_center(s, HF_TITLE, SW / 2, Y + 494, pal.text, 255, 0, t);
            char pos[64]; snprintf(pos, sizeof pos, "%d of %d", sel[tab] + 1, n);
            ht_center(s, HF_SMALL, SW / 2, Y + 544, pal.sub, 255, 0, pos);
        }
    }
    /* the letters */
    if (letters_on() && n) {
        int have[27] = { 0 };
        for (int i = 0; i < n; i++) have[letter_of(shelf[tab][i])] = 1;
        int cur = g ? letter_of(g) : -1;
        float lw = LET_W / 27.f;
        for (int i = 0; i < 27; i++) {
            char l[4] = { i ? (char)('A' + i - 1) : '#', 0 };
            float cx = LET_X + lw * (i + .5f);
            int on = i == (scrub ? scrub_letter : cur);
            if (on) hi_circle(s, cx, Y + LET_Y + 20.f, 19, 0, accent_ink, 255);
            ht_center(s, HF_SMALL, (int)cx, Y + LET_Y + 20 - ht_h(HF_SMALL) / 2, on ? 0xffffff : have[i] ? pal.sub : pal.faint, have[i] || on ? 255 : 150, 0, l);
        }
    } else if (n) {
        ht_center(s, HF_SMALL, SW / 2, Y + LET_Y + 8, pal.faint, 255, 0,
                  tab == SH_FAV ? "In the order you starred them  ·  X takes one off" : tab == SH_RECENT ? "The newest first" :
                  cfg.sort == 1 ? "Recently played first  ·  Settings changes the order" : "Most played first  ·  Settings changes the order");
    }
    /* what to do */
    {
        float p0 = press_of(110), p1 = press_of(111);
        int d = (int)(p0 * 5);
        card(s, 32 + d, Y + BAR_Y + d + (int)(p0 * 2), 104 - 2 * d, 104 - 2 * d, 30, (int)(5 - p0 * 4));
        icon(s, IC_GEAR, 84, Y + BAR_Y + 52.f + p0 * 2, 44, pal.sub, 255);
        d = (int)(p1 * 5);
        card(s, 150 + d, Y + BAR_Y + d + (int)(p1 * 2), 104 - 2 * d, 104 - 2 * d, 30, (int)(5 - p1 * 4));
        icon(s, IC_SUN, 202, Y + BAR_Y + 40.f + p1 * 2, 38, 0xD8941A, 255);
        for (int i = 0; i < 5; i++) hi_circle(s, 202 - 24 + i * 12.f, Y + BAR_Y + 78.f + p1 * 2, 4, 0, i < bri_lv ? 0xD8941A : pal.track, 255);
        if (g) {
            int pw = play_w(), ow = ht_w(HF_LABEL, "Options", 0) + 58 + 56;
            pill_button(s, SW - 32 - pw, Y + BAR_Y, pw, 104, play_label(g), "A", 1, press_of(113), HF_BTN);
            pill_button(s, SW - 32 - pw - 14 - ow, Y + BAR_Y, ow, 104, "Options", "Y", 0, press_of(112), HF_LABEL);
        }
    }
    /* a finger on the letters: the letter, big, over it */
    if (scrub && scrub_letter >= 0) {
        float lw = LET_W / 27.f, bx = clampf((float)tch.x, 90, SW - 90), by = Y + LET_Y - 96.f;
        hr_shadow(s, (int)bx - 60, (int)by - 60, 120, 120, 60, 20, 8, 120);
        hi_circle(s, bx, by, 60, 0, accent_ink, 255);
        char l[4] = { scrub_letter ? (char)('A' + scrub_letter - 1) : '#', 0 };
        ht_center(s, HF_HERO, (int)bx, (int)by - ht_h(HF_HERO) / 2, 0xffffff, 255, 0, l);
        (void)lw;
    }
    (void)alpha;
}

/* ---------------------------------------------------------------- moving */
static void select_to(int i, int quiet) {
    int n = shn[tab];
    if (!n) return;
    i = CLAMP(i, 0, n - 1);
    if (i == sel[tab]) return;
    int far = abs(i - sel[tab]) > 1;
    sel[tab] = i;
    sel_gap = T - sel_t; sel_t = T;
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
    if (spr_moving(&sc, .01f) || dragging || !shn[tab]) return;
    built = 0;
    for (int d = 1; d <= 4; d++) for (int sg = -1; sg <= 1; sg += 2) {
        int i = sel[tab] + sg * d;
        if (i < 0 || i >= shn[tab]) continue;
        Art *A = art_get(shelf[tab][i]);
        if (A && !A->sprite.px && A->state != 0) { cart_sprite(shelf[tab][i], 0); return; }
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
        if (A && A->state != 0) { game_prefetch(g); prefetched = g; }
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
    if (repeat(B_LEFT)) { if (sel[tab] > 0) select_to(sel[tab] - 1, 0); else if (pressed(B_LEFT)) { sfx(SFX_BUMP); buzz2(14, 0.3f); sc.v -= 4; } }
    if (repeat(B_RIGHT)) { if (sel[tab] < n - 1) select_to(sel[tab] + 1, 0); else if (pressed(B_RIGHT)) { sfx(SFX_BUMP); buzz2(14, 0.3f); sc.v += 4; } }
    if (repeat(B_UP) && letters_on() && g) {           /* up / down: a letter at a time */
        int l = letter_of(g);
        for (int i = sel[tab] - 1; i >= 0; i--) if (letter_of(shelf[tab][i]) != l) { int l2 = letter_of(shelf[tab][i]); while (i > 0 && letter_of(shelf[tab][i - 1]) == l2) i--; select_to(i, 0); break; }
    }
    if (repeat(B_DOWN) && letters_on() && g) {
        int l = letter_of(g);
        for (int i = sel[tab] + 1; i < n; i++) if (letter_of(shelf[tab][i]) != l) { select_to(i, 0); break; }
    }
    if (pressed(B_L1)) set_tab(tab - 1);
    if (pressed(B_R1)) set_tab(tab + 1);
    if (g && pressed(B_A)) { press_hit(113); start_launch(0); return; }
    if (g && pressed(B_Y)) { press_hit(112); go_page(P_GAME); return; }
    if (g && pressed(B_X)) { toggle_fav(g); return; }
    if (pressed(B_SELECT)) { press_hit(110); go_page(P_SETTINGS); return; }

    /* the finger */
    press_touch(101, 20, 256, 72, 104);
    press_touch(102, SW - 92, 256, 72, 104);
    press_touch(110, 32, BAR_Y, 104, 104);
    press_touch(111, 150, BAR_Y, 104, 104);
    int pw = play_w(), ow = ht_w(HF_LABEL, "Options", 0) + 58 + 56;
    press_touch(113, SW - 32 - pw, BAR_Y, pw, 104);
    press_touch(112, SW - 32 - pw - 14 - ow, BAR_Y, ow, 104);
    for (int i = 0; i < SH_COUNT; i++) press_touch(120 + i, tab_x(i), 24, TAB_SW, 92);

    /* a finger held on the middle cartridge: it sinks, and at half a second its page opens */
    if (n && g) {
        float cw, chh; cart_size(g, &cw, &chh);
        int cx0 = SW / 2 - (int)cw / 2, cy0 = CAR_Y - (int)chh / 2 - 16;
        press_touch(130, cx0, cy0, (int)cw, (int)chh);
        if (tch.down && !tch.moved && tch.id != drag_lock && tch.id != tch_dead && T - tch.t0 > 0.5 &&
            tch.x0 >= cx0 && tch.x0 < cx0 + cw && tch.y0 >= cy0 && tch.y0 < cy0 + chh) {
            drag_lock = tch.id; buzz(25, 0.4f); go_page(P_GAME); return;
        }
    }
    if (tch.down && tch.id != drag_lock && tch.id != tch_dead) {
        /* where did it go down */
        if (tch.y0 >= LET_Y - 16 && tch.y0 < LET_Y + 50 && letters_on() && n) { scrub = 1; drag_lock = tch.id; }
        else if (tch.y0 >= 128 && tch.y0 < 480 && tch.x0 > 100 && tch.x0 < SW - 100 && n && tch.moved) {
            if (abs(tch.x - tch.x0) >= abs(tch.y - tch.y0)) { dragging = 1; drag_base = sc.x; drag_lock = tch.id; }
            else if (tch.y - tch.y0 < -40 && abs(tch.x0 - SW / 2) < 140) {
                /* a flick up from the middle cartridge: in it goes */
                drag_lock = tch.id; start_launch(0); return;
            }
        }
    }
    if (scrub) {
        if (tch.down) {
            int l = CLAMP((int)((tch.x - LET_X) * 27 / LET_W), 0, 26);
            if (l != scrub_letter) {
                scrub_letter = l;
                /* the first game from that letter on (the next there is, when none starts with it) */
                for (int i = 0; i < n; i++) if (letter_of(shelf[tab][i]) >= l) { select_to(i, 1); break; }
                sfx_ex(SFX_NOTCH, 1.0f + l * 0.012f, 0.8f);
                dirty(1);
            }
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
            if (at != last_tick) { last_tick = at; sfx_ex(SFX_NOTCH, 1.1f, 0.5f); buzz(6, 0.2f); }
            if (at != sel[tab]) { sel[tab] = at; lift.x = 0.4f; lift.to = 1; sel_gap = T - sel_t; sel_t = T; home_selection_changed(); }
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
    if (tapped(20, 256, 72, 104)) { if (sel[tab] > 0) select_to(sel[tab] - 1, 0); else sfx(SFX_BUMP); return; }
    if (tapped(SW - 92, 256, 72, 104)) { if (sel[tab] < n - 1) select_to(sel[tab] + 1, 0); else sfx(SFX_BUMP); return; }
    for (int i = 0; i < SH_COUNT; i++) if (tapped(tab_x(i), 24, TAB_SW, 92)) { set_tab(i); return; }
    if (tapped(32, BAR_Y, 104, 104)) { go_page(P_SETTINGS); return; }
    if (tapped(150, BAR_Y, 104, 104)) { brightness_set(bri_lv % 5 + 1); sfx_ex(SFX_BRIGHT, 0.85f + bri_lv * 0.07f, 1); dirty(1); return; }
    if (g && tapped(SW - 32 - pw, BAR_Y, pw, 104)) { start_launch(0); return; }
    if (g && tapped(SW - 32 - pw - 14 - ow, BAR_Y, ow, 104)) { go_page(P_GAME); return; }
    /* a cartridge: the middle one plays; one to the side comes to the middle */
    if (n && tch.y0 >= 140 && tch.y0 < 470) {
        float best = 1e9; int bi = -1;
        for (int i = MAX(0, sel[tab] - 2); i <= MIN(n - 1, sel[tab] + 2); i++) {
            float dx, k; cart_place(i - sc.x, &dx, &k);
            float cw, ch; cart_size(shelf[tab][i], &cw, &ch);
            float d = fabsf(tch.x0 - (SW / 2 + dx));
            if (d < cw * k / 2 && d < best) { best = d; bi = i; }
        }
        if (bi == sel[tab]) start_launch(0);
        else if (bi >= 0) select_to(bi, 0);
    }
}
