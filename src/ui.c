// ui.c: DSi Menu 2, the core. Both of the RG DS Plus's screens are drawn at their own 1024x768: the top one the
// stage (the selected game, big), the bottom one the deck (everything you touch). Here: the state, the look (light
// or night by the clock, the user's colour), springs, input, the pieces every page draws with, the system menu,
// and the big moments: the start-up, a game going in and coming back out, sleep and waking.
// Nothing is drawn while nothing moves: a screen is drawn again only when something on it changed, and only that
// screen goes to the panel.
#define _GNU_SOURCE
#include "ui_int.h"
#include "gamecfg.h"
#include "power.h"
#include "img.h"
#include "sleepkey.h"
#include "rumble.h"
#include "dsi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

double T;
Pal pal;
uint32_t accent, accent_ink;
Game *shelf[SH_COUNT][MAXG];
int shn[SH_COUNT], tab = SH_DS, sel[SH_COUNT];
int page = P_HOME, page_from = P_HOME, seq = Q_NONE, sysmenu;
Spr page_k = { 1, 0, 1 };
double seq_t;
Input in, pin;
Touch tch;
int bri_lv = 4;
int tch_up, tch_up_moved;
int cover_fast;

static int dirty_s[2] = { 1, 1 };
static int exit_code = -1;
static double last_input;
static int pw_mode;
static Surf base[2]; static int base_ok, drew;   /* the pages under a big moment, drawn once when it starts */
static Surf tr[4]; static int tr_ok, tr_up;      /* a page change's pictures: old and new top, under and over bottom */
static Surf pre[2]; static Game *pre_g; static int pre_ok;   /* the selected game's page, drawn ahead on a still frame */
static Surf homesnap; static int homesnap_ok;    /* the deck as it was left (going back to it, unchanged) */
static int launch_how;
static Game *launch_g;

/* ---------------------------------------------------------------- motion */
void spr_step(Spr *s, float k, float c, float dt) {
    int n = (int)ceilf(dt * 480); if (n < 1) n = 1; if (n > 40) n = 40;
    float h = dt / n;
    for (int i = 0; i < n; i++) { float a = k * (s->to - s->x) - c * s->v; s->v += a * h; s->x += s->v * h; }
    if (fabsf(s->to - s->x) < 0.0005f && fabsf(s->v) < 0.005f) { s->x = s->to; s->v = 0; }
}
int spr_moving(const Spr *s, float eps) { return fabsf(s->to - s->x) > eps || fabsf(s->v) > eps * 4; }
void dirty(int screen) { if (screen >= 0 && screen < 2) dirty_s[screen] = 1; }
int frame_dirty(void) { return dirty_s[0] || dirty_s[1]; }
void buzz(int ms, float k) { if (cfg.rumble) rumble(ms, k); }
void buzz2(int ms, float k) { if (cfg.rumble) rumble_double(ms, k); }

/* ---------------------------------------------------------------- the look */
static const Pal LIGHT = { 0xF2F4F7, 0xE7EBF0, 0xFFFFFF, 0x1B2027, 0x56606D, 0xA3ACB7, 0xE5E9EE, 0xE1E5EB, 0xE0E4EA, 70, 0 };
static const Pal NIGHT = { 0x101318, 0x0B0D11, 0x1C2129, 0xF1F3F6, 0xA6AFBC, 0x5D6672, 0x262C36, 0x232933, 0x2B313B, 140, 1 };
static int theme_was = -1, theme_flip;
static float lin(int v) { float c = v / 255.f; return c <= .03928f ? c / 12.92f : powf((c + .055f) / 1.055f, 2.4f); }
void theme_update(void) {
    int night = cfg.theme == 2;
    if (cfg.theme == 0) {
        time_t t = time(0); struct tm lt; localtime_r(&t, &lt);
        if (getenv("DSIXL_FAKE_TIME")) lt.tm_hour = atoi(getenv("DSIXL_FAKE_TIME"));
        night = lt.tm_hour >= 19 || lt.tm_hour < 7;
    }
    pal = night ? NIGHT : LIGHT;
    accent = fav_color(cfg.color);
    /* dark enough for white letters on it (4.5:1), whatever the colour */
    accent_ink = accent;
    for (int i = 0; i < 12; i++) {
        float L = .2126f * lin(CR(accent_ink)) + .7152f * lin(CG(accent_ink)) + .0722f * lin(CB(accent_ink));
        if (1.05f / (L + .05f) >= 4.5f) break;
        accent_ink = h_mix(accent_ink, 0x000000, 0.12f);
    }
    int key = night * 100 + cfg.color;
    if (key != theme_was) { theme_flip = theme_was >= 0; theme_was = key; dirty(0); dirty(1); }
}
int theme_changed(void) { int r = theme_flip; theme_flip = 0; return r; }

/* ---------------------------------------------------------------- the shelves */
static const char *sort_key(const char *t) { return !strncasecmp(t, "the ", 4) ? t + 4 : t; }
static int by_title(const void *a, const void *b) {
    return strcasecmp(sort_key((*(Game *const *)a)->title), sort_key((*(Game *const *)b)->title));
}
static int by_fav(const void *a, const void *b) { return (*(Game *const *)a)->fav - (*(Game *const *)b)->fav; }
static int by_recent(const void *a, const void *b) {
    long long x = (*(Game *const *)a)->last_played, y = (*(Game *const *)b)->last_played;
    return x < y ? 1 : x > y ? -1 : by_title(a, b);
}
static int by_most(const void *a, const void *b) {
    long x = (*(Game *const *)a)->secs, y = (*(Game *const *)b)->secs;
    return x < y ? 1 : x > y ? -1 : by_title(a, b);
}
void shelves_build(void) {
    Game *keep[SH_COUNT];
    for (int s = 0; s < SH_COUNT; s++) { keep[s] = shn[s] ? shelf[s][CLAMP(sel[s], 0, shn[s] - 1)] : 0; shn[s] = 0; }
    for (int sys = 0; sys < SYS_COUNT; sys++) for (int i = 0; i < lib[sys].n; i++) {
        Game *g = &lib[sys].g[i];
        if (g->hidden) continue;
        int own = sys == SYS_NDS ? SH_DS : SH_GBA;
        if (shn[own] < MAXG) shelf[own][shn[own]++] = g;
        if (g->fav && shn[SH_FAV] < MAXG) shelf[SH_FAV][shn[SH_FAV]++] = g;
        if (g->last_played && shn[SH_RECENT] < MAXG) shelf[SH_RECENT][shn[SH_RECENT]++] = g;
    }
    int (*by)(const void *, const void *) = cfg.sort == 1 ? by_recent : cfg.sort == 2 ? by_most : by_title;
    qsort(shelf[SH_DS], (size_t)shn[SH_DS], sizeof(Game *), by);
    qsort(shelf[SH_GBA], (size_t)shn[SH_GBA], sizeof(Game *), by);
    qsort(shelf[SH_FAV], (size_t)shn[SH_FAV], sizeof(Game *), by_fav);
    qsort(shelf[SH_RECENT], (size_t)shn[SH_RECENT], sizeof(Game *), by_recent);
    if (shn[SH_RECENT] > 24) shn[SH_RECENT] = 24;
    for (int s = 0; s < SH_COUNT; s++) {
        sel[s] = CLAMP(sel[s], 0, MAX(0, shn[s] - 1));
        for (int i = 0; i < shn[s]; i++) if (shelf[s][i] == keep[s]) sel[s] = i;
    }
}
Game *cur_game(void) { return shn[tab] ? shelf[tab][CLAMP(sel[tab], 0, shn[tab] - 1)] : 0; }

/* ---------------------------------------------------------------- input */
static double held_since[B_COUNT], next_rep[B_COUNT];
int pressed(int b) { return (in.held & BIT(b)) && !(pin.held & BIT(b)); }
int released(int b) { return !(in.held & BIT(b)) && (pin.held & BIT(b)); }
int repeat(int b) {
    if (pressed(b)) { held_since[b] = T; next_rep[b] = T + 0.30; return 1; }
    if ((in.held & BIT(b)) && T >= next_rep[b]) {
        double held = T - held_since[b];
        next_rep[b] = T + (held > 1.6 ? 0.035 : held > 0.8 ? 0.06 : 0.09);   /* faster the longer it's held */
        return 1;
    }
    return 0;
}
static int tch_up_x, tch_up_y;
int tch_dead = -1;   /* tch_dead: a touch that was down when the page changed */
int tapped(int x, int y, int w, int h) {
    return tch_up && tch.id != tch_dead && !tch_up_moved && tch.x0 >= x && tch.x0 < x + w && tch.y0 >= y && tch.y0 < y + h &&
           tch_up_x >= x - 16 && tch_up_x < x + w + 16 && tch_up_y >= y - 16 && tch_up_y < y + h + 16;
}
int touching(int x, int y, int w, int h) {
    return tch.down && tch.id != tch_dead && !tch.moved && tch.x0 >= x && tch.x0 < x + w && tch.y0 >= y && tch.y0 < y + h;
}
static void touch_step(void) {
    tch_up = 0;
    int down = in.touch[1];
    if (down && !tch.down) {
        tch.down = 1; tch.x = tch.x0 = tch.lx = in.tx[1]; tch.y = tch.y0 = in.ty[1]; tch.moved = 0; tch.t0 = tch.lt = T; tch.vx = 0;
        tch.id++; last_input = T;
    } else if (down) {
        tch.x = in.tx[1]; tch.y = in.ty[1];
        if (abs(tch.x - tch.x0) > 14 || abs(tch.y - tch.y0) > 14) tch.moved = 1;
        double dt = T - tch.lt;
        if (dt > 0.004) { tch.vx = tch.vx * 0.5f + 0.5f * (float)((tch.x - tch.lx) / dt); tch.lx = tch.x; tch.lt = T; }
        last_input = T;
    } else if (tch.down) {
        tch.down = 0; tch_up = 1; tch_up_x = tch.x; tch_up_y = tch.y; tch_up_moved = tch.moved;
        if (T - tch.lt > 0.06) tch.vx = 0;
    }
}

/* ---------------------------------------------------------------- pressed buttons */
#define NPRESS 32
static struct { int id; Spr k; double t; } prs[NPRESS];
static int press_slot(int id) {
    for (int i = 0; i < NPRESS; i++) if (prs[i].id == id) { prs[i].t = T; return i; }
    int old = 0;
    for (int i = 0; i < NPRESS; i++) {
        if (!prs[i].id) { old = i; break; }
        if (prs[i].k.x == 0 && prs[i].k.to == 0 && prs[i].t < prs[old].t) old = i;
    }
    prs[old].id = id; spr_set(&prs[old].k, 0); prs[old].t = T;
    return old;
}
float press_of(int id) { return sat(prs[press_slot(id)].k.x); }
void press_hit(int id) { int i = press_slot(id); prs[i].k.x = 1; prs[i].k.v = 0; prs[i].k.to = 0; dirty(1); }
static void press_step(float dt) {
    for (int i = 0; i < NPRESS; i++)
        if (prs[i].id && spr_moving(&prs[i].k, .002f)) { spr_step(&prs[i].k, 900, 30, dt); dirty(1); }
}
void press_touch(int id, int x, int y, int w, int h) {
    int i = press_slot(id);
    float to = touching(x, y, w, h) ? 1 : 0;
    if (prs[i].k.to != to) {
        prs[i].k.to = to;
        if (to) { prs[i].k.v = 14; sfx_ex(SFX_KEY, 1.25f, 0.35f); }   /* (a quick sink: a press you feel) */
        dirty(1);
    }
}

/* ---------------------------------------------------------------- shared pieces */
void card(Surf *s, int x, int y, int w, int h, int r, int lift) {
    /* (one blur for every card: a lift is how dark and how far down; a new blur would be a new shadow to make) */
    if (lift > 0) hr_shadow(s, x, y, w, h, r, 15, lift + 1, pal.shadow_a * MIN(lift, 6) / 6);
    hr_fill(s, x, y, w, h, r, pal.card, 255);
}
void key_cap(Surf *s, int cx, int cy, int r, const char *k, uint32_t bg, uint32_t fg) {
    hi_circle(s, (float)cx, (float)cy, (float)r, 0, bg, 255);
    int f = r >= 20 ? HF_BODYB : HF_CAPS;
    ht_center(s, f, cx, cy - ht_h(f) / 2 - 1, fg, 255, 0, k);
}
/* a rounded button: primary (the user's colour) or plain (a card); key: its controller button's mark */
void pill_button(Surf *s, int x, int y, int w, int h, const char *label, const char *key, int primary, float press, int font) {
    int d = (int)(press * 5);
    int X = x + d, Y = y + d + (int)(press * 2), W = w - 2 * d, H = h - 2 * d, R = MIN(30, H / 2);
    if (primary) {
        hr_shadow(s, X, Y, W, H, R, 18, 9 - (int)(press * 6), (int)(110 * (1 - press * 0.6f)));
        hr_vgrad(s, X, Y, W, H, R, h_mix(accent_ink, 0xffffff, 0.12f), accent_ink, 255);
    } else {
        hr_shadow(s, X, Y, W, H, R, 15, 6 - (int)(press * 4), (int)(pal.shadow_a * (1 - press * 0.6f)));
        hr_fill(s, X, Y, W, H, R, pal.card, 255);
    }
    uint32_t fg = primary ? 0xffffff : pal.text;
    int tw = ht_w(font, label, 0), kw = key ? 58 : 0, cx = X + W / 2 - (tw + kw) / 2;
    if (key) {
        key_cap(s, cx + 22, Y + H / 2, 22, key, primary ? h_mix(accent_ink, 0xffffff, 0.26f) : pal.chip, primary ? 0xffffff : pal.sub);
        cx += kw;
    }
    ht(s, font, cx, Y + H / 2 - ht_h(font) / 2, fg, 255, 0, label);
}

/* icons, drawn from strokes (z: the box they fill) */
void icon(Surf *s, int which, float cx, float cy, float z, uint32_t c, int a) {
    float u = z / 24, w = 2.2f * u;
    switch (which) {
    case IC_GEAR:
        hi_circle(s, cx, cy, 7.2f * u, w * 1.15f, c, a);
        for (int i = 0; i < 8; i++) {
            float an = i * PI_F / 4;
            hi_line(s, cx + cosf(an) * 8.4f * u, cy + sinf(an) * 8.4f * u, cx + cosf(an) * 11 * u, cy + sinf(an) * 11 * u, 3.4f * u, c, a);
        }
        hi_circle(s, cx, cy, 2.6f * u, w, c, a);
        break;
    case IC_SUN:
        hi_circle(s, cx, cy, 5 * u, 0, c, a);
        for (int i = 0; i < 8; i++) {
            float an = i * PI_F / 4;
            hi_line(s, cx + cosf(an) * 8 * u, cy + sinf(an) * 8 * u, cx + cosf(an) * 10.5f * u, cy + sinf(an) * 10.5f * u, w, c, a);
        }
        break;
    case IC_MOON: case IC_SLEEP: {
        /* a crescent: a disc with a disc taken out (by coverage) */
        float R = 9 * u, ox = 4.5f * u, oy = -3.5f * u, Rr = 7.6f * u;
        int x0 = (int)(cx - R - 2), x1 = (int)(cx + R + 2), y0 = (int)(cy - R - 2), y1 = (int)(cy + R + 2);
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
            float dx = x + .5f - cx, dy = y + .5f - cy, d1 = sqrtf(dx * dx + dy * dy) - R;
            float ex = dx - ox, ey = dy - oy, d2 = Rr - sqrtf(ex * ex + ey * ey);
            float cov = .5f - (d1 > d2 ? d1 : d2);
            if (cov > 0) pblend(s, x, y, c, (int)(a * (cov > 1 ? 1 : cov)));
        }
        if (which == IC_SLEEP) {   /* a small z */
            float zx = cx + 7 * u, zy = cy - 10 * u;
            hi_line(s, zx, zy, zx + 5 * u, zy, w * .8f, c, a); hi_line(s, zx + 5 * u, zy, zx, zy + 5 * u, w * .8f, c, a);
            hi_line(s, zx, zy + 5 * u, zx + 5 * u, zy + 5 * u, w * .8f, c, a);
        }
        break;
    }
    case IC_STAR: case IC_STAR_O: {
        float p[20];
        for (int i = 0; i < 10; i++) {
            float an = -PI_F / 2 + i * PI_F / 5, r = (i & 1) ? 4.6f * u : 10.5f * u;
            p[2 * i] = cx + cosf(an) * r; p[2 * i + 1] = cy + 1 * u + sinf(an) * r;
        }
        if (which == IC_STAR) hi_poly(s, p, 10, c, a);
        else for (int i = 0; i < 10; i++) hi_line(s, p[2 * i], p[2 * i + 1], p[(2 * i + 2) % 20], p[(2 * i + 3) % 20], w, c, a);
        break;
    }
    case IC_BACK: case IC_LEFT:
        hi_line(s, cx + 3.5f * u, cy - 7 * u, cx - 3.5f * u, cy, w * 1.2f, c, a); hi_line(s, cx - 3.5f * u, cy, cx + 3.5f * u, cy + 7 * u, w * 1.2f, c, a);
        break;
    case IC_RIGHT:
        hi_line(s, cx - 3.5f * u, cy - 7 * u, cx + 3.5f * u, cy, w * 1.2f, c, a); hi_line(s, cx + 3.5f * u, cy, cx - 3.5f * u, cy + 7 * u, w * 1.2f, c, a);
        break;
    case IC_PLAY: { float p[6] = { cx - 5 * u, cy - 8 * u, cx + 8 * u, cy, cx - 5 * u, cy + 8 * u }; hi_poly(s, p, 3, c, a); break; }
    case IC_RESTART: {
        hi_arc(s, cx, cy, 8 * u, -PI_F * 0.3f, PI_F * 1.45f, w, c, a);
        float p[6] = { cx + 4.5f * u, cy - 11.5f * u, cx + 9.5f * u, cy - 6.5f * u, cx + 3 * u, cy - 4.5f * u };
        hi_poly(s, p, 3, c, a);
        break;
    }
    case IC_POWER:
        hi_arc(s, cx, cy + 1 * u, 8.5f * u, -PI_F * 0.32f, PI_F * 1.32f, w, c, a);
        hi_line(s, cx, cy - 10 * u, cx, cy + 1 * u, w, c, a);
        break;
    case IC_EXIT:
        hr_ring(s, (int)(cx - 10 * u), (int)(cy - 9 * u), (int)(13 * u), (int)(18 * u), (int)(3 * u), (int)(w + .5f), c, a);
        hi_line(s, cx - 1 * u, cy, cx + 10 * u, cy, w, c, a);
        hi_line(s, cx + 6 * u, cy - 4 * u, cx + 10 * u, cy, w, c, a); hi_line(s, cx + 6 * u, cy + 4 * u, cx + 10 * u, cy, w, c, a);
        break;
    case IC_WIFI:
        hi_arc(s, cx, cy + 7 * u, 13 * u, -PI_F * .78f, -PI_F * .22f, w, c, a);
        hi_arc(s, cx, cy + 7 * u, 8.5f * u, -PI_F * .78f, -PI_F * .22f, w, c, a);
        hi_circle(s, cx, cy + 6 * u, 1.9f * u, 0, c, a);
        break;
    case IC_IMAGE: {
        hr_ring(s, (int)(cx - 10 * u), (int)(cy - 8 * u), (int)(20 * u), (int)(16 * u), (int)(3 * u), (int)(w + .5f), c, a);
        float p[6] = { cx - 7 * u, cy + 5 * u, cx - 1 * u, cy - 2 * u, cx + 4 * u, cy + 5 * u };
        hi_poly(s, p, 3, c, a);
        hi_circle(s, cx + 4.5f * u, cy - 3 * u, 2 * u, 0, c, a);
        break;
    }
    case IC_CHECK:
        hi_line(s, cx - 7 * u, cy + 1 * u, cx - 2 * u, cy + 6 * u, w * 1.2f, c, a); hi_line(s, cx - 2 * u, cy + 6 * u, cx + 8 * u, cy - 6 * u, w * 1.2f, c, a);
        break;
    }
}

/* the focus ring: one, gliding from thing to thing (where the d-pad is) */
static Spr fx, fy, fw, fh; static float fr; static int f_on, f_was; static float f_a;
void focus_to(int x, int y, int w, int h, int r) {
    if (!f_was) { spr_set(&fx, (float)x); spr_set(&fy, (float)y); spr_set(&fw, (float)w); spr_set(&fh, (float)h); }
    if (fx.to != x || fy.to != y || fw.to != w || fh.to != h) dirty(1);
    fx.to = (float)x; fy.to = (float)y; fw.to = (float)w; fh.to = (float)h; fr = (float)r; f_on = 1; f_was = 1;
}
void focus_hide(void) { f_on = 0; }
static void focus_step(float dt) {
    Spr *sp[4] = { &fx, &fy, &fw, &fh };
    for (int i = 0; i < 4; i++) if (spr_moving(sp[i], .3f)) { spr_step(sp[i], K_SNAP * 1.3f, C_SNAP, dt); dirty(1); }
    float to = f_on ? 1 : 0;
    if (fabsf(f_a - to) > .001f) { f_a += (to - f_a) * MIN(1.0f, dt * 16); if (fabsf(f_a - to) < .02f) f_a = to; dirty(1); }
    if (!f_on && f_a <= 0) f_was = 0;
    f_on = 0;                                        /* (a page asks for it again every frame it wants it) */
}
void focus_ring(Surf *s) {
    if (f_a <= 0.01f) return;
    int a = (int)(255 * f_a), g = 7;
    hr_ring(s, (int)fx.x - g - 5, (int)fy.x - g - 5, (int)fw.x + 2 * g + 10, (int)fh.x + 2 * g + 10, (int)fr + g + 5, 5, accent, a / 5);
    hr_ring(s, (int)fx.x - g, (int)fy.x - g, (int)fw.x + 2 * g, (int)fh.x + 2 * g, (int)fr + g, 5, accent, a);
}
static int focus_visible(void) { return f_a > 0.01f; }

/* the status bar: the time, the date, Wi-Fi, the battery */
int clock24(void) {
    static double t_ = -100; static int v = 1;
    if (T - t_ > 5) { t_ = T; char b[16]; bset_get("/data/banana/settings.conf", "clock", b, sizeof b, "24h"); v = strcmp(b, "12h") != 0; }
    return v;
}
static int wifi_up(void) {
    static double t_ = -100; static int v;
    if (T - t_ > 4) {
        t_ = T; char b[16] = "";
        FILE *f = fopen("/sys/class/net/wlan0/operstate", "r");
        if (f) { if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); }
        v = !strncmp(b, "up", 2);
    }
    return v;
}
static void time_text(char *tm, int n, char *date, int dn) {
    time_t t = time(0); struct tm lt; localtime_r(&t, &lt);
    if (getenv("DSIXL_FAKE_TIME")) { lt.tm_hour = atoi(getenv("DSIXL_FAKE_TIME")); lt.tm_min = 42; lt.tm_wday = 6; lt.tm_mday = 24; lt.tm_mon = 9; }
    if (clock24()) snprintf(tm, (size_t)n, "%d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(tm, (size_t)n, "%d:%02d %s", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min, lt.tm_hour < 12 ? "AM" : "PM");
    static const char *wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    snprintf(date, (size_t)dn, "%s %d %s", wd[lt.tm_wday], lt.tm_mday, mo[lt.tm_mon]);
}
void status_bar(Surf *s, int y, int a) { dsi_status(s, y - 8, a); }

/* an image's corners rounded off (into its alpha) */
void img_round(Img *im, int r) {
    if (!im->px || r <= 0) return;
    r = MIN(r, MIN(im->w, im->h) / 2);
    for (int j = 0; j < r; j++) for (int i = 0; i < r; i++) {
        float dx = r - i - 0.5f, dy = r - j - 0.5f, cov = r - sqrtf(dx * dx + dy * dy) + 0.5f;
        if (cov >= 1) continue;
        float k = cov < 0 ? 0 : cov;
        int xs[2] = { i, im->w - 1 - i }, ys[2] = { j, im->h - 1 - j };
        for (int q = 0; q < 4; q++) {
            uint32_t *p = &im->px[(size_t)ys[q >> 1] * im->w + xs[q & 1]];
            *p = (*p & 0xffffff) | ((uint32_t)(CA(*p) * k) << 24);
        }
    }
}
uint32_t accent_text(void) { return pal.dark ? h_mix(accent, 0xffffff, 0.25f) : accent_ink; }

/* a game's picture at any size: its art (cropped to fill), else a cover made from its colours, its DS icon, title */
void game_cover(Surf *s, Game *g, int x, int y, int w, int h, int r, int a) {
    Art *A = art_get(g);
    if (A && A->state == 1 && A->cover.px) {
        Img *use = &A->hero;
        /* (two sizes kept: the one asked for again goes first) */
        if ((!use->px || use->w != w || use->h != h) && A->hero2.px && A->hero2.w == w && A->hero2.h == h) {
            Img t = A->hero; A->hero = A->hero2; A->hero2 = t;
        } else if (!cover_fast && use->px && (use->w != w || use->h != h)) { img_free(&A->hero2); A->hero2 = A->hero; A->hero.px = 0; }
        if (cover_fast && (!use->px || use->w != w || use->h != h)) {
            /* in motion: the picture scaled straight (nearest), cropped to fill by the clip */
            Surf c = *s; surf_clip(&c, x, y, w, h);
            const Img *src = use->px ? use : &A->cover;
            float sc = MAX((float)w / src->w, (float)h / src->h);
            hb_scaled(&c, src, x + (w - src->w * sc) / 2, y + (h - src->h * sc) / 2, src->w * sc, src->h * sc, a);
        } else {
            if (!use->px || use->w != w || use->h != h) { img_free(use); hb_fit(&A->cover, use, w, h, 1); img_round(use, r); }
            if (!use->px) return;
            hb_blit(s, use, x, y, a);
        }
        return;
    }
    uint32_t v = A && A->state ? A->vivid : 0x7d8aa0, d = A && A->state ? A->deep : 0x39414f;
    hr_vgrad(s, x, y, w, h, r, v, d, a);
    if (g->icon.px && w >= 96) {
        int z = MAX(1, MIN(w, h) / 3 / 32) * 32;      /* the DS's own 32x32 icon, by whole pixels */
        hb_scaled(s, &g->icon, (float)(x + (w - z) / 2), (float)(y + h / 2 - z * 3 / 4), (float)z, (float)z, a);
    }
    if (w >= 120) {
        int f = w >= 300 ? HF_TITLE : w >= 180 ? HF_LABEL : HF_SMALL;
        ht_wrap(s, f, x + 18, y + h - 16 - ht_h(f) * 2, w - 36, ht_h(f), 2, 0xffffff, a * 235 / 255, g->title);
    }
}

void cover_prepare(Game *g, int w, int h, int r) {
    Art *A = art_get(g);
    if (!A || A->state != 1 || !A->cover.px) return;
    if ((A->hero.px && A->hero.w == w && A->hero.h == h) || (A->hero2.px && A->hero2.w == w && A->hero2.h == h)) return;
    img_free(&A->hero2); hb_fit(&A->cover, &A->hero2, w, h, 1); img_round(&A->hero2, r);
}

/* both DS screens (a save state's), small: w wide each, the hinge between them */
void dss_screens(Surf *s, const Img *pic, Game *g, int x, int y, int w, int a) {
    int h = w * 3 / 4, gap = MAX(4, w / 28), pad = MAX(6, w / 20);
    hr_fill(s, x, y, w + 2 * pad, 2 * h + gap + 2 * pad, MAX(8, w / 14), 0x23262c, a);
    if (pic && pic->px) {
        Img upper = { 256, 192, pic->px };
        Img lower = { 256, 192, pic->px + 256 * 192 };
        blit_scaled(s, &upper, (float)(x + pad), (float)(y + pad), (float)w, (float)h, a, 1);
        blit_scaled(s, &lower, (float)(x + pad), (float)(y + pad + h + gap), (float)w, (float)h, a, 1);
    } else if (g) {
        rectf(s, x + pad, y + pad, w, h, 0x0e1014);
        rectf(s, x + pad, y + pad + h + gap, w, h, 0x0e1014);
        if (g->icon.px) {
            int z = MAX(1, h / 2 / 32) * 32;
            hb_scaled(s, &g->icon, (float)(x + pad + (w - z) / 2), (float)(y + pad + (h - z) / 2), (float)z, (float)z, a / 2);
        }
    }
}

void ago(long long when, char *out, int n) {
    long long d = (long long)time(0) - when;
    if (d < 90) snprintf(out, (size_t)n, "just now");
    else if (d < 3600) snprintf(out, (size_t)n, "%lld minutes ago", d / 60);
    else if (d < 7200) snprintf(out, (size_t)n, "an hour ago");
    else if (d < 86400) snprintf(out, (size_t)n, "%lld hours ago", d / 3600);
    else if (d < 2 * 86400) snprintf(out, (size_t)n, "yesterday");
    else if (d < 7 * 86400) snprintf(out, (size_t)n, "%lld days ago", d / 86400);
    else if (d < 14 * 86400) snprintf(out, (size_t)n, "last week");
    else if (d < 60 * 86400) snprintf(out, (size_t)n, "%lld weeks ago", d / (7 * 86400));
    else snprintf(out, (size_t)n, "%lld months ago", d / (30 * 86400));
}
void played_text(const Game *g, char *out, int n) {
    if (g->secs >= 3600) snprintf(out, (size_t)n, "%ld h", g->secs / 3600);
    else if (g->secs >= 60) snprintf(out, (size_t)n, "%ld min", g->secs / 60);
    else snprintf(out, (size_t)n, "a moment");
}

/* DraStic's states (BananaOS keeps DraStic in /data/drastic) */
int ds_resume_path(const Game *g, char *out, int n) {
    snprintf(out, (size_t)n, "%s/%s.resume.dss", bpath("/data/drastic/savestates"), g->base);
    return file_exists(out);
}
int ds_slot_path(const Game *g, int slot, char *out, int n) {
    snprintf(out, (size_t)n, "%s/%s_%d.dss", bpath("/data/drastic/savestates"), g->base, slot);
    return file_exists(out);
}
int resume_available(const Game *g, long long *when) {
    char p[800];
    if (g->sys == SYS_NDS) {
        if (!ds_resume_path(g, p, sizeof p)) return 0;
        if (when) *when = file_mtime(p);
        return 1;
    }
    /* RetroArch's automatic state, when it keeps one */
    static const char *dirs[] = { "/data/retroarch/states", "/data/states/gba", "/data/roms/gba" };
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s/%s.state.auto", bpath(dirs[i]), g->base);
        if (file_exists(p)) { if (when) *when = file_mtime(p); return 1; }
    }
    return 0;
}

/* ---------------------------------------------------------------- BananaOS's settings */
const char *bpath(const char *p) {
    static char b[4][700]; static int k;
    const char *d = getenv("DSIXL_DATA");
    if (!d || !*d || strncmp(p, "/data/", 6)) return p;
    k = (k + 1) % 4; snprintf(b[k], sizeof b[k], "%s%s", d, p + 5);
    return b[k];
}
void bset_get(const char *file, const char *key, char *out, int n, const char *def) {
    if (!bcfg_get(bpath(file), key, out, n)) snprintf(out, (size_t)n, "%s", def);
}
void bset_set(const char *file, const char *key, const char *val) {
    bcfg_set(bpath(file), key, val, strstr(file, "superdrastic.conf") || strstr(file, "drastic.cfg") ? KV_TIGHT :
                                    strstr(file, "retroarch.cfg") ? KV_QUOTED : KV_SPACED);
}
static const int BRI_PM[5] = { 100, 250, 450, 700, 1000 };
void brightness_set(int lv) {
    bri_lv = CLAMP(lv, 1, 5);
    homesnap_ok = 0;                                 /* (the deck shows the level) */
    char sh[400]; snprintf(sh, sizeof sh, "%s/brightness.sh", cfg.home);
    if (access(sh, X_OK)) return;
    char cmd[520]; snprintf(cmd, sizeof cmd, "'%s' set %d >/dev/null 2>&1 &", sh, bri_lv);
    if (system(cmd)) {}
}
static void brightness_read(void) {
    char b[16] = ""; FILE *f = fopen(bpath("/data/banana/backlight"), "r");
    if (f) { if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); }
    int pm = atoi(b); if (pm <= 0) return;
    int best = 0; for (int i = 1; i < 5; i++) if (abs(BRI_PM[i] - pm) < abs(BRI_PM[best] - pm)) best = i;
    bri_lv = best + 1;
}

/* ---------------------------------------------------------------- notices, and bananad's level card */
static char note_msg[160]; static double note_t = -10;
void notice(const char *msg) { snprintf(note_msg, sizeof note_msg, "%s", msg); note_t = T; dirty(0); }
static char osd_what[16]; static int osd_pct; static double osd_t = -10; static struct timespec osd_mt; static int osd_seen;
static void osd_poll(void) {
    static double t_; if (T - t_ < 0.08) return; t_ = T;
    struct stat st; if (stat("/run/banana/osd", &st)) { osd_seen = 1; return; }
    if (st.st_mtim.tv_sec == osd_mt.tv_sec && st.st_mtim.tv_nsec == osd_mt.tv_nsec) return;
    int first = !osd_seen; osd_seen = 1; osd_mt = st.st_mtim;
    if (first) return;                                   /* (one from before the menu started) */
    FILE *f = fopen("/run/banana/osd", "r"); if (!f) return;
    if (fscanf(f, "%15s %d", osd_what, &osd_pct) == 2) { osd_t = T; dirty(0); if (!strcmp(osd_what, "brightness")) brightness_read(); }
    fclose(f);
}
static int overlays_active(void) { return T - note_t < 3.3 || T - osd_t < 1.7; }
static void overlays_top(Surf *s) {
    float k = (float)(T - note_t);
    if (k < 3.2f) {
        float i = ease_out_back(sat(k / 0.35f)), out = 1 - smoothstep(2.8f, 3.2f, k);
        int w = MIN(900, ht_w(HF_LABEL, note_msg, 0) + 80), x = (SW - w) / 2, y = (int)(SH - 124 * i + 40 * (1 - out));
        int a = (int)(255 * out);
        hr_shadow(s, x, y, w, 72, 36, 18, 8, (int)(pal.shadow_a * out));
        hr_fill(s, x, y, w, 72, 36, pal.card, a);
        ht_center(s, HF_LABEL, SW / 2, y + 36 - ht_h(HF_LABEL) / 2, pal.text, a, 0, note_msg);
    }
    k = (float)(T - osd_t);
    if (k < 1.6f) {
        float i = ease_out_back(sat(k / 0.25f)), out = 1 - smoothstep(1.25f, 1.6f, k);
        int a = (int)(255 * out), w = 560, h = 96, x = (SW - w) / 2, y = (int)(-h + (h + 76) * i);
        hr_shadow(s, x, y, w, h, 30, 18, 8, (int)(pal.shadow_a * out));
        hr_fill(s, x, y, w, h, 30, pal.card, a);
        if (strcmp(osd_what, "brightness")) {   /* a speaker */
            float p[8] = { x + 30.f, y + 38.f, x + 42.f, y + 38.f, x + 58.f, y + 26.f, x + 58.f, y + 70.f };
            float q[8] = { x + 30.f, y + 38.f, x + 58.f, y + 70.f, x + 42.f, y + 58.f, x + 30.f, y + 58.f };
            hi_poly(s, p, 4, pal.sub, a); hi_poly(s, q, 4, pal.sub, a);
        } else icon(s, IC_SUN, (float)x + 46, (float)y + 48, 36, 0xE0A020, a);
        int n = 16, lit = (osd_pct * n + 50) / 100;
        for (int j = 0; j < n; j++) hr_fill(s, x + 96 + j * 27, y + 30, 19, 36, 6, j < lit ? accent : pal.track, a);
    }
}

/* the battery: a word when it runs low (15, 10, 5 %), a chime when the charger goes in */
static void battery_watch(void) {
    static double t_ = -100; static int was_chg = -1, warned = 101;
    if (T - t_ < 20 && t_ > 0) return;
    t_ = T;
    int chg = 0, lv = battery_level(&chg);
    if (lv < 0) return;
    if (was_chg == 0 && chg) { notice("Charging"); sfx_ex(SFX_DONE, 1.0f, 0.6f); warned = 101; }
    was_chg = chg;
    if (chg) return;
    static const int at[] = { 15, 10, 5 };
    for (int i = 2; i >= 0; i--) if (lv <= at[i] && warned > at[i]) {
        char b[64]; snprintf(b, sizeof b, "Battery low: %d%%. Time to charge.", lv);
        notice(b); sfx(SFX_ERROR); buzz2(25, 0.4f); warned = at[i]; break;
    }
}

/* ---------------------------------------------------------------- the system menu (START) */
static Spr sys_k; static int sys_sel;
static const struct { const char *label, *sub; int ic; uint32_t c; } SYSI[] = {
    { "Sleep", "Close the lid, or the power button", IC_SLEEP, 0x6C63D9 },
    { "Settings", "Screens, brightness, the look", IC_GEAR, 0 },
    { "Exit to BananaOS", "Its own menu", IC_EXIT, 0 },
    { "Power off", "", IC_POWER, 0xE5484D },
};
#define SYS_X 152
#define SYS_W 720
#define SYS_Y0 176
#define SYS_DY 128
#define SYS_H 112
void sysmenu_open(void) { if (tch.down) tch_dead = tch.id; sysmenu = 1; sys_k.to = 1; sys_sel = 0; sfx(SFX_OPEN); dirty(0); dirty(1); }
static void sysmenu_close(void) { sysmenu = 0; sys_k.to = 0; sfx(SFX_CLOSE); dirty(0); dirty(1); }
static void start_sleep(void);
static void sysmenu_do(int i) {
    press_hit(900 + i);
    switch (i) {
    case 0: sysmenu = 0; sys_k.to = 0; start_sleep(); buzz(30, 0.35f); break;
    case 1: sysmenu = 0; sys_k.to = 0; go_page(P_SETTINGS); break;
    case 2: sfx(SFX_BACK); exit_code = EXIT_TO_ES; break;
    case 3: sfx(SFX_SELECT); exit_code = EXIT_POWEROFF; break;
    }
}
static void sysmenu_update(float dt) {
    if (spr_moving(&sys_k, .002f)) { spr_step(&sys_k, K_SNAP, C_SNAP - 8, dt); dirty(0); dirty(1); }
    if (!sysmenu) return;
    if (pressed(B_B) || pressed(B_START)) { sysmenu_close(); return; }
    if (repeat(B_UP)) { if (sys_sel > 0) { sys_sel--; sfx(SFX_CURSOR); } else sfx(SFX_BUMP); }
    if (repeat(B_DOWN)) { if (sys_sel < 3) { sys_sel++; sfx(SFX_CURSOR); } else sfx(SFX_BUMP); }
    if (pressed(B_A)) { sysmenu_do(sys_sel); return; }
    for (int i = 0; i < 4; i++) {
        int y = SYS_Y0 + i * SYS_DY;
        press_touch(900 + i, SYS_X, y, SYS_W, SYS_H);
        if (tapped(SYS_X, y, SYS_W, SYS_H)) { sys_sel = i; sysmenu_do(i); return; }
    }
    if (tch_up && tch.id != tch_dead && !tch_up_moved && (tch.y0 < SYS_Y0 - 90 || tch.y0 > SYS_Y0 + 4 * SYS_DY)) { sysmenu_close(); return; }
    if (sys_k.x > 0.9f) focus_to(SYS_X, SYS_Y0 + sys_sel * SYS_DY, SYS_W, SYS_H, 30);
}
static void sysmenu_draw_bot(Surf *bot) {
    float k = sys_k.x; if (k <= 0.01f) return;
    uint32_t dim = pal.dark ? 0x000000 : 0x1b2027;
    rect_blend(bot, 0, 0, SW, SH, dim, (int)(130 * sat(k)));
    /* a DSi window rises, its buttons in it */
    int off = (int)((1 - k) * 420);
    int x = SYS_X - 40, y = SYS_Y0 - 104 + off, w = SYS_W + 80, h = 4 * SYS_DY + 104 + 8;
    dsi_window(bot, x, y, w, h, "HANDHELD", 255);
    Accent ac = dsi_accent();
    for (int i = 0; i < 4; i++) {
        float p = press_of(900 + i);
        int by = SYS_Y0 + i * SYS_DY + off + 4;
        dsi_face(bot, SYS_X, by, SYS_W, SYS_H, 26, i == sys_sel, p, 255);
        by += (int)(p * 4);
        uint32_t ic = SYSI[i].c ? SYSI[i].c : ac.base;
        hi_circle(bot, SYS_X + 62.f, by + SYS_H / 2.f, 38, 0, h_mix(ic, 0x000000, 0.25f), 255);
        hi_circle(bot, SYS_X + 62.f, by + SYS_H / 2.f, 36, 0, ic, 255);
        hi_circle(bot, SYS_X + 62.f, by + SYS_H / 2.f - 14, 22, 0, 0xffffff, 50);
        icon(bot, SYSI[i].ic, SYS_X + 62.f, by + SYS_H / 2.f, 38, 0xffffff, 255);
        int ty = SYSI[i].sub[0] ? by + SYS_H / 2 - ht_h(HF_MD) / 2 - 20 : by + SYS_H / 2 - ht_h(HF_MD) / 2;
        ht(bot, HF_MD, SYS_X + 124, ty, i == 3 ? 0xc8343a : D_TEXT, 255, 0, SYSI[i].label);
        if (SYSI[i].sub[0]) ht(bot, HF_XS, SYS_X + 124, ty + ht_h(HF_MD) - 4, D_TEXT_SOFT, 255, 0, SYSI[i].sub);
        if (i == sys_sel) dsi_glyph(bot, SYS_X + SYS_W - 68, by + SYS_H / 2 - 22, "A", ac.base, 255);
    }
}
static void sysmenu_draw_top(Surf *top) {
    float k = sys_k.x; if (k <= 0.01f) return;
    int a = (int)(255 * sat(k));
    rect_blend(top, 0, 0, SW, SH, pal.dark ? 0x000000 : 0x1b2027, (int)(232 * sat(k)));
    /* the top screen: the time, big */
    char tm[24], date[32]; time_text(tm, sizeof tm, date, sizeof date);
    ht_center(top, HF_CLOCK, SW / 2, 220 + (int)((1 - k) * 40), 0xffffff, a, 0, tm);
    ht_center(top, HF_TITLE, SW / 2, 420 + (int)((1 - k) * 40), 0xffffff, a * 4 / 5, 0, date);
    int chg = 0, lv = battery_level(&chg);
    if (lv >= 0) {
        char b[48]; snprintf(b, sizeof b, chg ? "%d%%  charging" : "%d%% battery", lv);
        ht_center(top, HF_BODYB, SW / 2, 490 + (int)((1 - k) * 40), 0xffffff, a * 3 / 5, 0, b);
    }
}

/* two colours mixed, a of 256 of the first */
static inline uint32_t mix2(uint32_t c, uint32_t e, uint32_t a, uint32_t ia) {
    return ((((c & 0xff00ff) * a + (e & 0xff00ff) * ia) >> 8) & 0xff00ff) | ((((c & 0x00ff00) * a + (e & 0x00ff00) * ia) >> 8) & 0x00ff00);
}

/* ---------------------------------------------------------------- pages */
void go_page(int p) {
    if (p == page) return;
    page_from = page; page = p; spr_set(&page_k, 0); page_k.to = 1; tr_ok = 0;
    if (tch.down) tch_dead = tch.id;                 /* (the finger that changed the page taps nothing on the new one) */
    if (p == P_GAME) game_enter();
    if (p == P_SETTINGS) set_enter();
    if (p == P_HOME) home_enter();
    sfx(p == P_HOME ? SFX_CLOSE : SFX_OPEN);
    dirty(0); dirty(1);
}
static void page_draw_top(Surf *s, int p) {
    if (p == P_GAME) game_draw_top(s); else if (p == P_SETTINGS) set_draw_top(s); else home_draw_top(s);
}
static void page_draw_bot(Surf *s, int p, float yoff, int a) {
    if (p == P_GAME) game_draw_bot(s, yoff, a); else if (p == P_SETTINGS) set_draw_bot(s, yoff, a); else home_draw_bot(s, yoff, a);
}

void prerender_game(Game *g) {
    if (!pre[0].px) { surf_alloc(&pre[0], SW, SH); surf_alloc(&pre[1], SW, SH); }
    if (!pre[0].px || !pre[1].px) return;
    surf_noclip(&pre[0]); surf_noclip(&pre[1]);
    game_draw_top(&pre[0]); status_bar(&pre[0], 16, 255);
    fill(&pre[1], 0x000000);
    game_draw_bot(&pre[1], 1, 255);
    pre_g = g; pre_ok = 1;
}
void prerender_forget(void) { pre_ok = 0; homesnap_ok = 0; }

/* ---------------------------------------------------------------- the big moments */
/* the launch, the DSi's way, at the panels' resolution: the cartridge squashes and jumps off the deck (a trail
 * behind it), the console's slot rises along the top screen's bottom edge, the cartridge comes up turning, hangs a
 * moment and slams down into the slot (dust, a shake, the lamp blinking); then both screens close onto it (an iris
 * across the two, as one picture) and the last dot of light goes out like an old TV's. Back from a game it all goes
 * the other way: the iris opens out of the slot, the cartridge pops out, turns, and drops through the hinge onto its
 * place on the deck. */
#define L_SQUASH 0.08f
#define L_UP     0.30f      /* off the top of the bottom screen */
#define L_APEX   0.56f      /* risen high on the top screen */
#define L_HANG   0.67f
#define L_SLAM   0.77f      /* home, in the slot */
#define L_IRIS   0.98f      /* the screens close onto it */
#define L_SHUT   1.48f
#define L_END    1.64f      /* (a moment of black: the game takes the screens from black) */
#define R_OPEN   0.52f      /* back: the iris all the way open */
#define R_OUT    0.80f      /* the cartridge out of the slot, at the top */
#define R_DROP   1.00f      /* gone through the hinge */
#define R_LAND   1.22f      /* on its place on the deck */
#define R_END    1.55f
#define SLOT_H 56
#define HINGE 104           /* the hinge between the screens: the iris runs across it as across one picture */
static float slot_top(float rise) { return SH - rise * SLOT_H; }
static float slot_open_y(float rise) { return slot_top(rise) + 12; }
static float top_scale(const Game *g) { return g->sys == SYS_NDS ? 10.5f : 8.9f; }
static float slot_home_y(const Game *g) {   /* the cartridge's middle when its lower 42% is in the slot */
    float w, h, d; cart3d_size(g->sys, &w, &h, &d);
    return slot_open_y(1) + h * top_scale(g) * (0.42f - 0.5f);
}
static void draw_slot_back(Surf *s, float rise, float slit_w) {
    if (rise <= 0.001f) return;
    int y = (int)roundf(slot_top(rise));
    rect_vgrad(s, 0, y, SW, 20, 0xa3a7ae, 0x868a92, 255);
    rect_blend(s, 0, y, SW, 4, 0xc4c8ce, 255);
    hr_fill(s, (int)(SW / 2 - slit_w / 2), y + 4, (int)slit_w, 14, 6, 0x0c0d0f, 255);
    rect_blend(s, (int)(SW / 2 - slit_w / 2) + 8, y + 4, (int)slit_w - 16, 4, 0x2a2c30, 255);
}
static void draw_slot_front(Surf *s, float rise, float slit_w, int led) {
    if (rise <= 0.001f) return;
    int y = (int)roundf(slot_top(rise));
    rect_blend(s, (int)(SW / 2 - slit_w / 2), y + 16, (int)slit_w, 4, 0x8e929a, 255);
    rect_vgrad(s, 0, y + 20, SW, SH - (y + 20) + 32, 0x5e6168, 0x3a3c41, 255);
    rect_blend(s, 0, y + 20, SW, 4, 0x70747b, 255);
    uint32_t lc = led == 2 ? 0x46f04a : led == 1 ? 0x2fae32 : 0x2a3a2a;
    hi_circle(s, 928, y + 38, 7, 0, lc, 255);
    if (led == 2) h_glow(s, 928, y + 38, 26, 0x46f04a, 110);
    ht(s, HF_XS, 56, y + 22, 0x9da1a8, 255, 2, "DSi XL");
}

/* particles: the slam's dust and sparkles */
typedef struct { float x, y, vx, vy, age, life, size; uint32_t c; int spark; } Part;
static Part parts[96]; static int nparts;
static void spawn(float x, float y, float vx, float vy, float life, float size, uint32_t c, int spark) {
    if (nparts >= 96) return;
    parts[nparts++] = (Part){ x, y, vx, vy, 0, life, size, c, spark };
}
static void parts_step(float dt) {
    for (int i = 0; i < nparts; i++) {
        Part *p = &parts[i];
        p->age += dt; p->x += p->vx * dt; p->y += p->vy * dt;
        if (!p->spark) { p->vy += 1400 * dt; p->vx *= 1 - 2.5f * dt; }
        if (p->age >= p->life) { parts[i--] = parts[--nparts]; }
    }
}
static void parts_draw(Surf *s) {
    for (int i = 0; i < nparts; i++) {
        Part *p = &parts[i]; float k = 1 - p->age / p->life;
        if (p->spark) dsi_sparkle(s, p->x, p->y, p->size * (0.4f + 0.6f * k), 0xffffff, (int)(255 * MIN(1.0f, k * 1.6f)));
        else hi_circle(s, p->x, p->y, p->size * (0.6f + 0.4f * k), 0, p->c, (int)(200 * k));
    }
}
static double shake_t = -10; static float shake_amp;
static void slam(void) {
    sfx(SFX_SLAM); buzz(90, 1.0f);
    shake_t = T; shake_amp = 18;
    for (int i = 0; i < 26; i++) {
        float side = (i & 1) ? 1 : -1, sp = 120 + (hash3(i, 1, 7) % 280);
        spawn(SW / 2 + side * (80 + (hash3(i, 2, 7) % 160)), slot_open_y(1) - 4, side * sp, -(60 + (float)(hash3(i, 3, 7) % 160)),
              0.5f + (hash3(i, 4, 7) % 40) / 100.0f, 10 + (hash3(i, 5, 7) % 14), 0x8a8478, 0);
    }
    for (int i = 0; i < 8; i++) spawn(SW / 2 + ((int)(hash3(i, 9, 3) % 480) - 240), slot_open_y(1) - 24 - (hash3(i, 8, 3) % 120), 0, -40, 0.5f, 5, 0xffffff, 1);
}

/* the iris: both screens one picture (the hinge between), the circle round (cx, cy) in the top screen's pixels */
static float iris_cy(const Game *g) {
    float w, h, d; cart3d_size(g->sys, &w, &h, &d);
    float home = slot_home_y(g);
    return MIN((home - h * top_scale(g) * 0.5f + slot_open_y(1)) * 0.5f, SH - 1 - 136 * 1.12f);
}
static float iris_full(float cy) { float dy = SH + HINGE + SH - cy; return sqrtf(520.f * 520.f + dy * dy); }
static void iris_screen(Surf *s, float y0, float cy, float r) {
    int rim = (int)(170 * clampf(r / 120, 0, 1));
    for (int y = 0; y < SH; y++) {
        float dy = fabsf(y0 + y + 0.5f - cy);
        if (dy >= r) { rectf(s, 0, y, SW, 1, 0x000000); continue; }
        float h = sqrtf(r * r - dy * dy), hn = dy + 4 < r ? sqrtf(r * r - (dy + 4) * (dy + 4)) : 0;
        int x0 = (int)roundf(SW / 2 - h), x1 = (int)roundf(SW / 2 + h), n0 = (int)roundf(SW / 2 - hn), n1 = (int)roundf(SW / 2 + hn);
        if (x0 > 0) rectf(s, 0, y, MIN(x0, SW), 1, 0x000000);
        if (x1 < SW) rectf(s, MAX(x1, 0), y, SW - MAX(x1, 0), 1, 0x000000);
        if (rim > 0 && x1 > x0) {
            int lw = MAX(6, n0 - x0), rw = MAX(6, x1 - n1);
            if (lw + rw >= x1 - x0) rect_blend(s, x0, y, x1 - x0, 1, 0xffffff, rim);
            else { rect_blend(s, x0, y, lw, 1, 0xffffff, rim); rect_blend(s, x1 - rw, y, rw, 1, 0xffffff, rim); }
        }
    }
}
static void iris(Surf *top, Surf *bot, float cy, float r, float dot) {
    iris_screen(top, 0, cy, MAX(r, 0));
    iris_screen(bot, SH + HINGE, cy, MAX(r, 0));
    if (dot > 0.05f) { hi_circle(top, SW / 2, cy, dot, 0, 0xffffff, 255); h_glow(top, SW / 2, cy, dot * 3, 0xffffff, 90); }
}
static float iris_close_r(float k, float full) {
    const float F = 136;
    if (k < 0.55f) return lerpf(full, F, ease_in_out(k / 0.55f));
    if (k < 0.78f) return lerpf(F, F * 0.86f, ease_out_cubic((k - 0.55f) / 0.23f));
    return F * 0.86f * (1 - ease_in_cubic((k - 0.78f) / 0.22f));
}
/* the deck's pose of the selected cartridge */
static void deck_pose(Game *g, CartPose *p) {
    float x, y, w, h; home_cart_rect(g, &x, &y, &w, &h);
    float mw, mh, md; cart3d_size(g->sys, &mw, &mh, &md);
    memset(p, 0, sizeof *p);
    p->sx = x + w / 2; p->sy = y + h / 2 + 16; p->scale = w / mw; p->alpha = 1; p->bright = 1; p->pitch = 0.08f;
}
static void shake(Surf *s) {
    float k = (float)(T - shake_t) / 0.3f;
    if (k < 0 || k >= 1) return;
    int dy = (int)(shake_amp * (1 - k) * sinf(k * 40)), dx = (int)(shake_amp * 0.4f * (1 - k) * sinf(k * 31 + 1));
    surf_shift(s, dx, dy, 0x3a3c41);
}
static void launch_draw(Surf *top, Surf *bot, Game *g, float t) {
    float mw, mh, md; cart3d_size(g->sys, &mw, &mh, &md);
    CartPose base; deck_pose(g, &base);
    /* the deck: the cartridge squashes, then jumps off the top edge, a trail behind it */
    if (t < L_UP) {
        CartPose p = base;
        if (t < L_SQUASH) { float k = sinf(t / L_SQUASH * PI_F); p.sy += 12 * k; p.stretch_y = 1 - 0.1f * k; p.stretch_x = 1 + 0.06f * k; }
        else {
            float k = ease_in_cubic((t - L_SQUASH) / (L_UP - L_SQUASH));
            float y1 = -mh * p.scale - 80;
            p.sy = lerpf(base.sy, y1, k); p.scale *= 1 + 0.35f * k; p.pitch = lerpf(p.pitch, 0.25f, k);
            for (int j = 3; j >= 1; j--) {
                CartPose q = p; float kk = ease_in_cubic(MAX(0.0f, (t - j * 0.018f - L_SQUASH) / (L_UP - L_SQUASH)));
                q.sy = lerpf(base.sy, y1, kk); q.alpha = 0.18f * (4 - j) / 3.0f;
                cart3d_draw(bot, g, &q);
            }
        }
        cart3d_draw(bot, g, &p);
    }
    /* the top: the slot rises; the cartridge comes up from below turning, hangs, and slams down into it */
    float rise = ease_out_back(clampf((t - 0.12f) / 0.26f, 0, 1));
    float sc = top_scale(g), slit = mw * sc + 32;
    float apex_y = 264, home = slot_home_y(g);
    rect_blend(top, 0, 0, SW, SH, 0x000000, (int)(70 * smoothstep(0.1f, 0.5f, t)));   /* (the stage dims a little) */
    draw_slot_back(top, rise, slit);
    if (t >= L_UP - 0.05f) {
        CartPose p; memset(&p, 0, sizeof p);
        p.sx = SW / 2; p.alpha = 1; p.bright = 1.05f; p.scale = sc;
        if (t < L_APEX) {
            float k = ease_out_cubic((t - (L_UP - 0.05f)) / (L_APEX - L_UP + 0.05f));
            p.sy = lerpf(SH + mh * sc * 0.6f, apex_y, k);
            p.yaw = (1 - k) * 2 * PI_F;                          /* a full turn on the way up */
            p.pitch = (1 - k) * 0.5f; p.roll = sinf(k * PI_F) * 0.08f;
        } else if (t < L_HANG) {
            float k = (t - L_APEX) / (L_HANG - L_APEX);
            p.sy = apex_y - sinf(k * PI_F) * 12;
            p.roll = sinf(k * PI_F * 2) * 0.03f; p.pitch = -0.05f * sinf(k * PI_F);
        } else if (t < L_SLAM) {
            float k = ease_in_cubic((t - L_HANG) / (L_SLAM - L_HANG));
            p.sy = lerpf(apex_y - 12, home, k);
            p.scale = sc * (1 + 0.04f * k);
            for (int j = 0; j < 6; j++) {   /* motion lines on the way down */
                float lx = SW / 2 - mw * sc * 0.45f + j * mw * sc * 0.18f, top_y = p.sy - mh * sc * 0.5f;
                hi_line(top, lx, top_y - 88 * k, lx, top_y - 16, 4, 0x9a9a9a, (int)(140 * k));
            }
        } else {
            float k = (t - L_SLAM) / 0.22f;   /* home: a small bounce, then still */
            p.sy = home - (k < 1 ? sinf(k * PI_F) * 12 * (1 - k) : 0);
            p.scale = sc * (k < 1 ? 1 + 0.03f * (1 - k) * cosf(k * PI_F * 3) : 1);
        }
        Surf clip = *top; surf_clip(&clip, 0, 0, SW, rise > 0.001f ? (int)roundf(slot_open_y(rise)) : SH);
        cart3d_draw(&clip, g, &p);
    }
    draw_slot_front(top, rise, slit, t >= L_SLAM ? ((int)((t - L_SLAM) * 8) % 2 ? 2 : 1) : 0);
    parts_draw(top);
    shake(top);
    if (t >= L_IRIS) {
        float k = clampf((t - L_IRIS) / (L_SHUT - L_IRIS), 0, 1), cy = iris_cy(g);
        float r = iris_close_r(k, iris_full(cy));
        float dot = t > L_SHUT ? 10 * (1 - (t - L_SHUT) / (L_END - L_SHUT - 0.04f)) : 0;
        iris(top, bot, cy, k >= 1 ? 0 : r, dot);
        if (k >= 1) { fill(bot, 0); if (dot <= 0.05f) fill(top, 0); }
    }
}
static void return_draw(Surf *top, Surf *bot, Game *g, float t) {
    float mw, mh, md; cart3d_size(g->sys, &mw, &mh, &md);
    float sc = top_scale(g), slit = mw * sc + 32, home = slot_home_y(g), apex_y = 300;
    /* the slot sinks away once the cartridge is out */
    float rise = t < R_OUT ? 1 : 1 - ease_in_cubic(clampf((t - R_OUT) / 0.2f, 0, 1));
    rect_blend(top, 0, 0, SW, SH, 0x000000, (int)(70 * (1 - smoothstep(R_OUT, R_DROP, t))));
    draw_slot_back(top, rise, slit);
    if (t < R_DROP) {
        CartPose p; memset(&p, 0, sizeof p);
        p.sx = SW / 2; p.alpha = 1; p.bright = 1.05f; p.scale = sc;
        if (t < R_OPEN) p.sy = home;
        else if (t < R_OUT) {
            float k = ease_out_back(clampf((t - R_OPEN) / (R_OUT - R_OPEN), 0, 1));
            p.sy = lerpf(home, apex_y, k); p.yaw = k * PI_F; p.pitch = -0.1f * sinf(k * PI_F);
        } else {
            float k = ease_in_cubic((t - R_OUT) / (R_DROP - R_OUT));
            p.sy = lerpf(apex_y, SH + mh * sc * 0.7f, k); p.yaw = PI_F + k * PI_F; p.pitch = 0.3f * k;
        }
        Surf clip = *top; surf_clip(&clip, 0, 0, SW, t < R_OUT ? (int)roundf(slot_open_y(rise)) : SH);
        cart3d_draw(&clip, g, &p);
    }
    draw_slot_front(top, rise, slit, t < R_OPEN ? 2 : 0);
    /* the deck: it drops in from the top edge and lands on its place: a squash, the jelly */
    if (t > R_DROP - 0.04f && t < R_END) {
        CartPose base; deck_pose(g, &base);
        CartPose p = base;
        if (t < R_LAND) {
            float k = ease_in_cubic(clampf((t - (R_DROP - 0.04f)) / (R_LAND - R_DROP + 0.04f), 0, 1));
            p.sy = lerpf(-mh * base.scale, base.sy, k); p.pitch = 0.3f * (1 - k); p.scale *= 1 + 0.2f * (1 - k);
        } else {
            float k = (t - R_LAND) / (R_END - R_LAND);
            float sq = expf(-6 * k) * sinf(k * 22) * 0.12f;
            p.stretch_y = 1 - sq; p.stretch_x = 1 + sq * 0.6f;
        }
        cart3d_draw(bot, g, &p);
    }
    if (t < R_OPEN) {
        float k = ease_in_out(clampf((t - 0.06f) / (R_OPEN - 0.06f), 0, 1)), cy = iris_cy(g);
        iris(top, bot, cy, k * iris_full(cy) + (k > 0 ? 0 : 0), 0);
    }
}

void start_launch(int how) {
    Game *g = cur_game(); if (!g || seq != Q_NONE) return;
    launch_g = g; launch_how = how; seq = Q_LAUNCH; seq_t = 0;
    sfx(SFX_SELECT); music_fade(0, 0.5f);
    dirty(0); dirty(1);
}
static void write_launch(void) {
    const char *p = getenv("DSIXL_LAUNCH_FILE"); if (!p) p = "/tmp/dsixl-launch";
    FILE *f = fopen(p, "w");
    if (!f) return;
    char how[32] = "play";
    if (launch_how == 1) snprintf(how, sizeof how, "start");
    else if (launch_how >= 2) snprintf(how, sizeof how, "slot %d", launch_how - 2);
    fprintf(f, "%s\n%s\n%s\n%s\n", sys_short(launch_g->sys), launch_g->path, launch_g->file, how);
    fclose(f);
    library_mark_played(launch_g);
    cfg.sys = tab; snprintf(cfg.last[launch_g->sys], sizeof cfg.last[0], "%s", launch_g->file);
    if (tab == SH_FAV) snprintf(cfg.last_fav, sizeof cfg.last_fav, "%s", launch_g->file);
    snprintf(cfg.recent, sizeof cfg.recent, "%s", launch_g->file); cfg.recent_sys = launch_g->sys;
    state_save();
}
static void seq_draw(Surf *top, Surf *bot) {
    cover_fast = 1;
    float t = (float)seq_t;
    Game *g = launch_g ? launch_g : cur_game();
    Art *A = g ? art_get(g) : 0;
    if (seq == Q_LAUNCH && g) launch_draw(top, bot, g, t);
    if (seq == Q_RETURN && g) return_draw(top, bot, g, t);
    if (seq == Q_SLEEP || seq == Q_ASLEEP) {
        /* good night: the screens dim, a moon rises in a soft light, then it too goes out */
        float k = smoothstep(0, 0.45f, t), m = ease_out_back(sat((t - 0.12f) / 0.55f)), out = smoothstep(0.95f, 1.5f, t);
        rect_blend(top, 0, 0, SW, SH, 0x05060a, (int)(240 * k));
        rect_blend(bot, 0, 0, SW, SH, 0x05060a, (int)(240 * k));
        float my = lerpf(SH / 2.f + 70, SH / 2.f - 30, m);
        int ma = (int)(255 * sat(m) * (1 - out));
        h_glow(top, SW / 2.f, my, 230, 0x6C63D9, ma * 2 / 5);
        icon(top, IC_MOON, SW / 2.f, my, 160, 0xF5E7A8, ma);
        ht_center(top, HF_TITLE, SW / 2, (int)my + 120, 0xC9CCE0, ma * 4 / 5, 2, "Good night");
        if (out > 0) { rect_blend(top, 0, 0, SW, SH, 0, (int)(255 * out)); rect_blend(bot, 0, 0, SW, SH, 0, (int)(255 * out)); }
        if (seq == Q_ASLEEP) { fill(top, 0); fill(bot, 0); }
    }
    cover_fast = 0;
    if (seq == Q_WAKE) {
        /* good morning: light from the hinge, the screens come up out of it */
        float k = 1 - ease_out_cubic(sat(t / 0.65f));
        float gl = sinf(sat(t / 0.75f) * PI_F);
        if (k > 0) { rect_blend(top, 0, 0, SW, SH, 0, (int)(255 * k)); rect_blend(bot, 0, 0, SW, SH, 0, (int)(255 * k)); }
        if (gl > 0.01f) {
            /* (a band of light along the hinge, row by row: the cheap way to a soft glow this wide) */
            for (int j = 0; j < 300; j++) {
                float f = 1 - j / 300.f; int a = (int)(120 * gl * f * f);
                if (a <= 0) break;
                rect_blend(top, 0, SH - 1 - j, SW, 1, 0xfff4d8, a);
                rect_blend(bot, 0, j, SW, 1, 0xfff4d8, a);
            }
        }
    }
}
static void start_sleep(void) {
    if (seq != Q_NONE) return;
    sysmenu = 0; sys_k.to = 0;
    seq = Q_SLEEP; seq_t = 0; sfx(SFX_SLEEP); music_fade(0, 0.6f); dirty(0); dirty(1);
}
static void start_wake(void) {
    sysmenu = 0; spr_set(&sys_k, 0);
    seq = Q_WAKE; seq_t = 0; pw_mode = PW_FULL; last_input = T;
    theme_update();
    sfx(SFX_WAKE); if (cfg.music) music_fade(0.55f, 1.0f);
    dirty(0); dirty(1);
}
static int crossed(float t, float dt, float at) { return t >= at && t - dt < at; }
static void seq_update(float dt) {
    seq_t += dt;
    float t = (float)seq_t;
    if (seq != Q_ASLEEP || t < 0.1f) { dirty(0); dirty(1); }
    switch (seq) {
    case Q_BOOT: if (t > 1.6f) seq = Q_NONE; break;
    case Q_LAUNCH:
        parts_step(dt);
        if (crossed(t, dt, L_SQUASH)) { sfx(SFX_LAUNCH); buzz(40, 0.5f); }
        if (crossed(t, dt, L_SLAM)) slam();
        if (crossed(t, dt, L_IRIS)) sfx(SFX_IRIS_IN);
        if (t >= L_END) { write_launch(); exit_code = EXIT_LAUNCH; }
        break;
    case Q_RETURN:
        parts_step(dt);
        if (crossed(t, dt, 0.04f)) sfx(SFX_IRIS_OUT);
        if (crossed(t, dt, R_OPEN)) { sfx_ex(SFX_WHOOSH, 1.2f, 0.7f); buzz(20, 0.3f); }
        if (crossed(t, dt, R_LAND)) { sfx(SFX_SLAM); buzz(45, 0.6f); }
        if (t > R_END) { seq = Q_NONE; launch_g = 0; if (cfg.music) music_play(); }
        break;
    case Q_SLEEP:
        if (t > 1.55f) {
            seq = Q_ASLEEP; seq_t = 0;
            if (sleepkey_tap()) power_screens(0);     /* no uinput: dark at least, until a button */
        }
        break;
    case Q_ASLEEP:
        /* bananad sleeps the handheld (and stops the menu); back here means it woke (ui_resumed); or it didn't sleep
         * after all: then any button */
        if (t > 3 && ((in.held & ~pin.held) || tch_up)) { power_screens(1); start_wake(); }
        break;
    case Q_WAKE: if (t > 0.8f) seq = Q_NONE; break;
    }
}

/* the start-up: both screens come up out of black; the name pops in on the stage with the chime, then lifts away
 * and the menu is there */
static void boot_draw(Surf *top, Surf *bot) {
    float t = (float)seq_t;
    float out = smoothstep(1.05f, 1.5f, t);
    rect_blend(top, 0, 0, SW, SH, pal.bg, (int)(255 * (1 - out)));
    rect_blend(bot, 0, 0, SW, SH, pal.bg, (int)(255 * (1 - out)));
    float s = ease_out_back(sat((t - 0.15f) / 0.45f));
    int a = (int)(255 * sat((t - 0.15f) / 0.2f) * (1 - out));
    int y = SH / 2 - 40 - (int)(out * 60);
    /* the mark: two screens and the hinge */
    int mw = (int)(132 * s), mh = (int)(96 * s);
    if (mw > 4 && a > 0) {
        hr_fill(top, SW / 2 - mw / 2, y - 2 * mh - 34, mw, mh, MAX(2, (int)(18 * s)), accent_ink, a);
        hr_fill(top, SW / 2 - mw / 2, y - mh - 22, mw, mh, MAX(2, (int)(18 * s)), h_mix(accent, 0xffffff, 0.35f), a);
    }
    ht_center(top, HF_HUGE, SW / 2, y + 10, pal.text, a, -1, "DSi Menu");
    ht_center(top, HF_CAPS, SW / 2, y + 120, pal.sub, a * 3 / 4, 6, "FOR THE RG DS PLUS");
    /* the deck: a hint of the cartridges sliding up */
    float b = 1 - smoothstep(0, 0.4f, t);
    if (b > 0) { rect_blend(top, 0, 0, SW, SH, 0, (int)(255 * b)); rect_blend(bot, 0, 0, SW, SH, 0, (int)(255 * b)); }
}

/* ---------------------------------------------------------------- start, and the frame */
void ui_init(const UiStart *st) {
    theme_update();
    art_init();
    sleepkey_init();
    brightness_read();
    {   /* (the shadows every page uses: their tiles made now, while the start-up plays) */
        Surf t; surf_alloc(&t, 8, 8);
        static const int rb[][3] = { { 30, 15, 104 }, { 26, 15, 210 }, { 24, 15, 230 }, { 30, 18, 590 }, { 30, 15, 720 },
                                     { 40, 30, 1024 }, { 44, 30, 880 }, { 26, 44, 360 }, { 24, 36, 300 }, { 18, 36, 576 },
                                     { 39, 12, 196 }, { 36, 18, 300 }, { 30, 15, 400 }, { 60, 20, 120 }, { 19, 8, 120 } };
        for (int i = 0; i < ARRAY_LEN(rb); i++) hr_shadow(&t, 0, 0, rb[i][2], rb[i][2], rb[i][0], rb[i][1], 0, 1);
        surf_free(&t);
    }
    tab = CLAMP(cfg.sys, 0, SH_COUNT - 1);
    shelves_build();
    for (int s = 0; s < SH_COUNT; s++) for (int i = 0; i < shn[s]; i++) {   /* the selection as it was */
        Game *g = shelf[s][i];
        if (s == SH_FAV && !strcmp(g->file, cfg.last_fav)) sel[s] = i;
        if ((s == SH_DS || s == SH_GBA) && !strcmp(g->file, cfg.last[g->sys])) sel[s] = i;
    }
    if (!shn[tab]) for (int s = 0; s < SH_COUNT; s++) if (shn[s]) { tab = s; break; }
    if (st->ret_file[0]) {
        /* back from a game: it comes out on the shelf it went in from (else its own) */
        int found = 0;
        for (int pass = 0; pass < 2 && !found; pass++)
            for (int s = 0; s < SH_COUNT && !found; s++) {
                if (pass == 0 && s != tab) continue;
                for (int i = 0; i < shn[s]; i++)
                    if (shelf[s][i]->sys == st->ret_sys && !strcmp(shelf[s][i]->file, st->ret_file)) {
                        tab = s; sel[s] = i; launch_g = shelf[s][i]; found = 1; break;
                    }
            }
        if (found) { seq = Q_RETURN; seq_t = 0; }
    } else if (!st->no_boot) {
        seq = Q_BOOT; seq_t = 0; sfx(SFX_BOOT);
    }
    home_enter();
    if (cfg.music && seq != Q_RETURN) music_play();
}

int ui_drew(void) { return drew; }
int ui_power(void) { return pw_mode; }
int ui_asleep(void) { return seq == Q_ASLEEP; }
void ui_idle_input(const Input *i) {
    if (i->held || i->touch[0] || i->touch[1]) { pw_mode = PW_FULL; last_input = T; dirty(0); dirty(1); }
}
void ui_resumed(void) {
    plat_log("ui: back from a sleep");
    if (seq == Q_LAUNCH) return;
    power_screens(1);
    start_wake();
}
static void power_step(void) {
    double idle = T - last_input;
    int m = PW_FULL;
    if (seq != Q_NONE || sysmenu || page_k.x < 1) m = PW_FULL;
    else if (cfg.screen_off_after > 0 && idle > cfg.screen_off_after) m = PW_OFF;
    else if (cfg.dim_after > 0 && idle > cfg.dim_after) m = PW_DIM;
    else if (idle > 20) m = PW_CALM;
    if (m >= PW_DIM && pw_mode < PW_DIM) music_fade(0.2f, 1.5f);
    if (m < PW_DIM && pw_mode >= PW_DIM && cfg.music) music_fade(0.55f, 0.6f);
    pw_mode = m;
}

int ui_frame(const Input *input, double dt, Surf *top, Surf *bot) {
    pin = in; in = *input;
    float fdt = (float)MIN(dt, 0.05);
    T += dt;
    if (in.quit) return EXIT_QUIT;
    touch_step();
    if (in.held != pin.held) last_input = T;
    /* dimmed: the first press only brightens it */
    static int swallow;
    if (pw_mode >= PW_DIM && ((in.held & ~pin.held) || tch.down)) { pw_mode = PW_FULL; swallow = 1; dirty(0); dirty(1); }
    if (swallow) { if (!in.held && !tch.down) swallow = 0; in.held = pin.held = 0; tch_up = 0; tch.moved = 1; }

    static int minute = -1;
    { time_t t = time(0); struct tm lt; localtime_r(&t, &lt); if (lt.tm_min != minute) { minute = lt.tm_min; theme_update(); dirty(0); } }
    if (theme_changed()) { home_selection_changed(); pre_ok = homesnap_ok = 0; }
    if (art_step()) { home_selection_changed(); base_ok = 0; pre_ok = homesnap_ok = 0; dirty(0); dirty(1); }
    osd_poll();
    rumble_tick();
    battery_watch();
    getart_poll();
    press_step(fdt);
    if (spr_moving(&page_k, .001f)) { spr_step(&page_k, K_SNAP, C_SNAP, fdt); dirty(0); dirty(1); }

    if (seq == Q_BOOT) {
        seq_update(fdt);
        if (((in.held & ~pin.held) || tch_up) && seq_t < 1.05) seq_t = 1.05;   /* (a press skips it) */
        pin.held = in.held; tch_up = 0;                                         /* (and does nothing else) */
    } else if (seq != Q_NONE) { seq_update(fdt); if (spr_moving(&sys_k, .002f)) spr_step(&sys_k, K_SNAP, C_SNAP - 8, fdt); }
    if (seq == Q_NONE || seq == Q_BOOT) {
        if (sysmenu || sys_k.x > 0.01f) sysmenu_update(fdt);
        else if (seq == Q_NONE) {
            if (pressed(B_START) && page != P_SETTINGS) sysmenu_open();
            else if (page == P_HOME) home_update(fdt);
            else if (page == P_GAME) game_update(fdt);
            else set_update(fdt);
        }
    }
    if (page != P_HOME || sysmenu || seq != Q_NONE) home_update(-fdt);   /* (its motion goes on under the others) */
    focus_step(fdt);

    if ((seq != Q_NONE && !(seq == Q_ASLEEP && seq_t > 0.1)) || (focus_visible() && f_a < 1)) { dirty(0); dirty(1); }
    if (overlays_active()) dirty(0);

    /* draw what changed (what asks to be drawn again while it's drawn is for the next frame) */
    int want[2] = { dirty_s[0], dirty_s[1] };
    dirty_s[0] = dirty_s[1] = 0;
    float k = page_k.x;
    int moving = k < 0.999f && page_from != page;
    int big = seq == Q_LAUNCH || seq == Q_RETURN || seq == Q_SLEEP || seq == Q_WAKE || seq == Q_ASLEEP;
    int sheet = !big && sys_k.x > 0.01f && !moving;     /* the START menu over a page that stays as it is */
    if (!big && !sheet) base_ok = 0;
    if (sheet && (want[0] || want[1])) {
        if (!base_ok) {
            if (!base[0].px) { surf_alloc(&base[0], SW, SH); surf_alloc(&base[1], SW, SH); }
            surf_noclip(&base[0]); surf_noclip(&base[1]);
            page_draw_top(&base[0], page); status_bar(&base[0], 16, 255);
            page_draw_bot(&base[1], page, 0, 255);
            base_ok = 1;
        }
        if (want[0]) { surf_copy(top, &base[0]); sysmenu_draw_top(top); overlays_top(top); plat_damage(0, 0, SH); }
        if (want[1]) { surf_copy(bot, &base[1]); sysmenu_draw_bot(bot); focus_ring(bot); plat_damage(1, 0, SH); }
        drew = want[0] | want[1] << 1;
        want[0] = want[1] = 0;
    }
    if (big && want[0] && want[1]) {
        /* nothing moves on the pages under these: drawn once, copied after */
        if (!base_ok) {
            if (!base[0].px) { surf_alloc(&base[0], SW, SH); surf_alloc(&base[1], SW, SH); }
            surf_noclip(&base[0]); surf_noclip(&base[1]);
            page_draw_top(&base[0], page); status_bar(&base[0], 16, 255);
            page_draw_bot(&base[1], page, 0, 255);
            base_ok = 1;
        }
        surf_copy(top, &base[0]); surf_copy(bot, &base[1]);
        if (sys_k.x > 0.01f) { sysmenu_draw_top(top); sysmenu_draw_bot(bot); }
        seq_draw(top, bot);
        want[0] = want[1] = 0;
        plat_damage(0, 0, SH); plat_damage(1, 0, SH);
        drew = 3;
    }
    if (moving && !tr_ok && want[0] && want[1]) {
        /* a page change: the four pictures it moves between drawn once (nothing moves on them meanwhile) */
        for (int i = 0; i < 4; i++) { if (!tr[i].px) surf_alloc(&tr[i], SW, SH); surf_noclip(&tr[i]); }
        /* (the old page's are on the screens already: taken from there) */
        surf_copy(&tr[0], top);
        tr_up = page != P_HOME && !(page == P_GAME && page_from == P_SETTINGS);
        int ready = page == P_GAME && page_from == P_HOME && pre_ok && pre_g == cur_game();
        if (ready) surf_copy(&tr[1], &pre[0]);
        else { page_draw_top(&tr[1], page); status_bar(&tr[1], 16, 255); }
        if (tr_up) {
            surf_copy(&tr[2], bot);
            if (page_from == P_HOME) { if (!homesnap.px) surf_alloc(&homesnap, SW, SH); if (homesnap.px) { surf_copy(&homesnap, bot); homesnap_ok = 1; } }
        } else if (page == P_HOME && homesnap_ok) surf_copy(&tr[2], &homesnap);
        else page_draw_bot(&tr[2], page, 0, 255);
        if (ready) surf_copy(&tr[3], &pre[1]);
        else { fill(&tr[3], 0x000000); page_draw_bot(&tr[3], tr_up ? page : page_from, 1, 255); }   /* (1: the sheet's rounded top) */
        tr_ok = 1;
    }
    if (!moving) tr_ok = 0;
    if (want[0]) {
        surf_noclip(top);
        if (moving && tr_ok) {
            /* the old page fades out over the new one */
            int a = (int)(256 * (1 - sat(k)));
            /* ...and the new one comes in from the side it belongs to (deeper pages from the right) */
            int sh = (int)((1 - ease_out_cubic(k)) * 56) * (tr_up ? 1 : -1);
            uint32_t ia = (uint32_t)(256 - a);
            for (int y = 0; y < SH; y++) {
                uint32_t *d = top->px + (size_t)y * top->pitch;
                const uint32_t *o = tr[0].px + (size_t)y * tr[0].pitch, *n = tr[1].px + (size_t)y * tr[1].pitch;
                /* (the new page moved by sh: its first or last column carried to the edge) */
                int x0 = sh > 0 ? sh : 0, x1 = sh < 0 ? SW + sh : SW;
                for (int x = 0; x < x0; x++) d[x] = mix2(o[x], n[0], (uint32_t)a, ia);
                const uint32_t *ns = n - sh;
                for (int x = x0; x < x1; x++) d[x] = mix2(o[x], ns[x], (uint32_t)a, ia);
                for (int x = x1; x < SW; x++) d[x] = mix2(o[x], n[SW - 1], (uint32_t)a, ia);
            }
        } else { page_draw_top(top, page); status_bar(top, 16, 255); }
    }
    if (want[1]) {
        surf_noclip(bot);
        if (moving && tr_ok) {
            /* a page comes up over the deck like a sheet; going back, it slides down off it */
            float e = tr_up ? ease_out_cubic(k) : 1 - ease_in_cubic(k);
            int off = (int)((1 - e) * SH);
            surf_copy(bot, &tr[2]);
            rect_blend(bot, 0, 0, SW, off, 0x000000, (int)(110 * e));
            /* a soft shadow over the sheet's edge, then the sheet (its rows, moved down) */
            for (int j = 1; j <= 24 && off - j >= 0; j++) rect_blend(bot, 0, off - j, SW, 1, 0x000000, (24 - j) * 3);
            for (int y = off; y < SH; y++) {
                const uint32_t *src = tr[3].px + (size_t)(y - off + 1 < SH ? y - off + 1 : SH - 1) * tr[3].pitch;
                uint32_t *d = bot->px + (size_t)y * bot->pitch;
                if (y - off < 41) {   /* (the sheet's rounded corners: its own pixels, where they aren't the black under it) */
                    for (int x = 0; x < SW; x++) if (src[x]) d[x] = src[x];
                } else memcpy(d, src, SW * 4);
            }
        } else if (seq == Q_BOOT) {
            /* the start-up: the deck rises into place as the name lifts away */
            float r = 1 - ease_out_cubic(sat(((float)seq_t - 0.95f) / 0.55f));
            fill(bot, pal.bg);
            page_draw_bot(bot, page, r * 160, 255);
        } else page_draw_bot(bot, page, 0, 255);
        if (!moving && !sysmenu) focus_ring(bot);
    }
    if (sys_k.x > 0.01f) { if (want[0]) sysmenu_draw_top(top); if (want[1]) { sysmenu_draw_bot(bot); focus_ring(bot); } }
    if (want[0]) overlays_top(top);
    if (want[0] && want[1]) { if (seq == Q_BOOT) boot_draw(top, bot); else if (seq != Q_NONE) seq_draw(top, bot); }
    for (int s = 0; s < 2; s++) if (want[s]) plat_damage(s, 0, SH);
    if (!big && !sheet) drew = want[0] | want[1] << 1;
    plat_damage(-1, 0, 0);                            /* (damage on: an unchanged screen isn't sent at all) */

    power_step();
    if (exit_code >= 0) { int e = exit_code; exit_code = -1; state_save(); return e; }
    return -1;
}
