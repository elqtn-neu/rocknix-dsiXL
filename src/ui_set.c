// ui_set.c: Settings (the gear, SELECT, or START > Settings). The top screen: the time, big, and what the setting
// in focus does; the bottom one the settings, a list: left / right (or a tap) changes one, at once, where BananaOS
// keeps it (its own Settings reads the same files).
#define _GNU_SOURCE
#include "ui_int.h"
#include "gamecfg.h"
#include "power.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { R_HDR, R_EMU, R_BRI, R_THEME, R_COLOR, R_SFX, R_MUSIC, R_CLOCK, R_DIM, R_OFF, R_ABOUT };
typedef struct { int kind, sys, opt; const char *label, *help; } Row;
static Row rows[40]; static int nr, foc = 1;
static Spr scroll;                      /* the list's offset (pixels) */
static int dragging; static float drag_base;
static int choice[SYS_COUNT][GOPT_MAX];
static const GameOpt *opts[SYS_COUNT]; static int nopts[SYS_COUNT];
static Spr val_k[40];                   /* each row's value nudging as it changes */

#define LIST_Y 128
#define ROW_H 96
#define HDR_H 64
static const int DIM_S[] = { 0, 30, 60, 120, 300 };
static const char *DIM_N[] = { "Never", "30 seconds", "1 minute", "2 minutes", "5 minutes" };
static const int OFF_S[] = { 0, 60, 120, 300, 600 };
static const char *OFF_N[] = { "Never", "1 minute", "2 minutes", "5 minutes", "10 minutes" };
static const char *THEME_N[] = { "By the clock", "Light", "Night" };

static void add(int kind, int sys, int opt, const char *label, const char *help) {
    if (nr < 40) rows[nr++] = (Row){ kind, sys, opt, label, help };
}
static int find_opt(int sys, const char *key) {
    for (int i = 0; i < nopts[sys]; i++) if (!strcmp(opts[sys][i].key, key)) return i;
    return -1;
}
void set_enter(void) {
    for (int s = 0; s < SYS_COUNT; s++) { nopts[s] = gamecfg_opts(s, &opts[s]); gamecfg_load(s, choice[s]); }
    nr = 0;
    int o;
    add(R_HDR, 0, 0, "SCREENS", 0);
    if ((o = find_opt(SYS_NDS, "shader")) >= 0) add(R_EMU, SYS_NDS, o, "DS screen look", 0);
    add(R_BRI, 0, 0, "Brightness", "How bright both screens are. The sun on the home screen steps through it too.");
    add(R_THEME, 0, 0, "Theme", "By the clock: light in the day, night from seven in the evening to seven in the morning.");
    add(R_COLOR, 0, 0, "Your color", "Your DS favorite color: the menu's buttons, the focus, the highlights.");
    add(R_HDR, 0, 0, "SOUND", 0);
    add(R_SFX, 0, 0, "Sounds", "The menu's sounds: the ticks, the chimes, a game going in.");
    add(R_MUSIC, 0, 0, "Music", "The menu's tune, softly, while you choose.");
    add(R_HDR, 0, 0, "NINTENDO DS", 0);
    for (int i = 0; i < nopts[SYS_NDS]; i++) if (strcmp(opts[SYS_NDS][i].key, "shader")) add(R_EMU, SYS_NDS, i, opts[SYS_NDS][i].label, 0);
    add(R_HDR, 0, 0, "GAME BOY ADVANCE", 0);
    for (int i = 0; i < nopts[SYS_GBA]; i++) add(R_EMU, SYS_GBA, i, opts[SYS_GBA][i].label, 0);
    add(R_HDR, 0, 0, "HANDHELD", 0);
    add(R_CLOCK, 0, 0, "Clock", "24-hour or 12-hour time, here and in BananaOS.");
    add(R_DIM, 0, 0, "Dim the screens", "Untouched this long, the screens dim. Any button brings them back.");
    add(R_OFF, 0, 0, "Screens off", "Untouched this long, the screens go off (the handheld stays on).");
    add(R_ABOUT, 0, 0, "About", "DSi Menu, made for the Anbernic RG DS Plus.");
    foc = 1; spr_set(&scroll, 0);
    for (int i = 0; i < nr; i++) spr_set(&val_k[i], 0);
}

static int row_y(int i) {
    int y = 0;
    for (int j = 0; j < i; j++) y += rows[j].kind == R_HDR ? HDR_H : ROW_H;
    return y;
}
static int list_h(void) { return row_y(nr); }

/* ---------------------------------------------------------------- values */
static int dim_i(void) { int b = 0; for (int i = 0; i < 5; i++) if (DIM_S[i] == cfg.dim_after) b = i; return b; }
static int off_i(void) { int b = 0; for (int i = 0; i < 5; i++) if (OFF_S[i] == cfg.screen_off_after) b = i; return b; }
static void value_text(const Row *r, char *out, int n) {
    switch (r->kind) {
    case R_EMU: snprintf(out, (size_t)n, "%s", opts[r->sys][r->opt].names[choice[r->sys][r->opt]]); break;
    case R_THEME: snprintf(out, (size_t)n, "%s", THEME_N[CLAMP(cfg.theme, 0, 2)]); break;
    case R_COLOR: snprintf(out, (size_t)n, "%s", fav_color_name(cfg.color)); break;
    case R_SFX: snprintf(out, (size_t)n, cfg.sfx ? "On" : "Off"); break;
    case R_MUSIC: snprintf(out, (size_t)n, cfg.music ? "On" : "Off"); break;
    case R_CLOCK: snprintf(out, (size_t)n, clock24() ? "24-hour" : "12-hour"); break;
    case R_DIM: snprintf(out, (size_t)n, "%s", DIM_N[dim_i()]); break;
    case R_OFF: snprintf(out, (size_t)n, "%s", OFF_N[off_i()]); break;
    case R_ABOUT: snprintf(out, (size_t)n, "Version %s", DSIXL_VERSION); break;
    default: out[0] = 0;
    }
}
static int step(int i, int d) {
    Row *r = &rows[i];
    switch (r->kind) {
    case R_EMU: {
        const GameOpt *o = &opts[r->sys][r->opt];
        choice[r->sys][r->opt] = (choice[r->sys][r->opt] + d + o->n) % o->n;
        gamecfg_save(r->sys, choice[r->sys]);
        break;
    }
    case R_BRI: {
        int v = bri_lv + d;
        if (v < 1 || v > 5) return 0;
        d = d > 0 ? 1 : -1;
        brightness_set(v); sfx_ex(SFX_BRIGHT, 0.85f + v * 0.07f, 1);
        return 2;
    }
    case R_THEME: cfg.theme = (cfg.theme + d + 3) % 3; config_save(); theme_update(); break;
    case R_COLOR: cfg.color = (cfg.color + d + 16) % 16; config_save(); theme_update(); break;
    case R_SFX: cfg.sfx = !cfg.sfx; config_save(); break;
    case R_MUSIC: cfg.music = !cfg.music; config_save(); if (cfg.music) music_play(); else music_fade(0, 0.4f); break;
    case R_CLOCK: bset_set("/data/banana/settings.conf", "clock", clock24() ? "12h" : "24h"); break;
    case R_DIM: cfg.dim_after = DIM_S[(dim_i() + d + 5) % 5]; config_save(); break;
    case R_OFF: cfg.screen_off_after = OFF_S[(off_i() + d + 5) % 5]; config_save(); break;
    default: return 0;
    }
    return 1;
}
static void change(int i, int d) {
    int r = step(i, d);
    if (!r) { sfx(SFX_BUMP); val_k[i].v += d * 30; dirty(1); return; }
    if (r == 1) sfx_ex(SFX_TOGGLE, d > 0 ? 1.0f : 0.9f, 1);
    val_k[i].x = (float)d * 14; val_k[i].v = 0; val_k[i].to = 0;
    dirty(0); dirty(1);
}

/* ---------------------------------------------------------------- the top screen */
void set_draw_top(Surf *s) {
    surf_noclip(s);
    rect_vgrad(s, 0, 0, SW, SH, h_mix(pal.bg, accent, pal.dark ? 0.14f : 0.12f), pal.bg, 255);
    time_t t = time(0); struct tm lt; localtime_r(&t, &lt);
    if (getenv("DSIXL_FAKE_TIME")) { lt.tm_hour = atoi(getenv("DSIXL_FAKE_TIME")); lt.tm_min = 42; lt.tm_wday = 6; lt.tm_mday = 24; lt.tm_mon = 9; }
    char tm[16], date[64];
    if (clock24()) snprintf(tm, sizeof tm, "%d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(tm, sizeof tm, "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
    strftime(date, sizeof date, "%A, %e %B", &lt);
    int e = ht(s, HF_CLOCK, 64, 96, pal.text, 255, -2, tm);
    if (!clock24()) ht(s, HF_TITLE, e + 16, 96 + ht_h(HF_CLOCK) - ht_h(HF_TITLE) - 22, pal.sub, 255, 0, lt.tm_hour < 12 ? "AM" : "PM");
    ht(s, HF_TITLE, 70, 96 + ht_h(HF_CLOCK) - 6, pal.sub, 255, 0, date[0] == ' ' ? date + 1 : date);
    /* what the focused one does */
    if (foc < 0 || foc >= nr) return;
    Row *r = &rows[foc];
    card(s, 64, 404, SW - 128, 300, 28, 6);
    int x = 104, w = SW - 128 - 80;
    if (r->kind == R_COLOR) {
        hi_circle(s, 64 + 40 + 70.f, 404 + 150.f, 70, 0, accent, 255);
        x += 170; w -= 170;
    }
    ht(s, HF_CAPS, x, 444, accent_text(), 255, 3, r->kind == R_EMU ? (r->sys == SYS_NDS ? "NINTENDO DS" : "GAME BOY ADVANCE") : "SETTINGS");
    char v[96]; value_text(r, v, sizeof v);
    ht(s, HF_TITLE, x, 476, pal.text, 255, 0, r->kind == R_BRI ? "Brightness" : r->label);
    const char *help = r->help, *note = 0;
    if (r->kind == R_EMU) { const GameOpt *o = &opts[r->sys][r->opt]; help = o->help; note = o->notes[choice[r->sys][r->opt]]; }
    int y = 476 + ht_h(HF_TITLE) + 12;
    if (help) y += 34 * ht_wrap(s, HF_BODY, x, y, w, 34, 3, pal.sub, 255, help) + 8;
    if (note) {
        char b[200]; snprintf(b, sizeof b, "%s: %s", v, note);
        ht_wrap(s, HF_BODYB, x, y, w, 34, 2, pal.text, 255, b);
    }
    if (r->kind == R_ABOUT) {
        char b[160]; float fg = 0, tg = 0;
        snprintf(b, sizeof b, "%d DS games, %d GBA games", lib[SYS_NDS].n, lib[SYS_GBA].n);
        ht(s, HF_BODYB, x, y, pal.text, 255, 0, b);
        if (!disk_free_gb(bpath("/data"), &fg, &tg)) { snprintf(b, sizeof b, "%.1f GB free of %.0f GB on the card", fg, tg); ht(s, HF_BODY, x, y + 38, pal.sub, 255, 0, b); }
    }
}

/* ---------------------------------------------------------------- the bottom screen */
void set_draw_bot(Surf *s, float yoff, int alpha) {
    int Y = (int)yoff; (void)alpha;
    if (Y > 0) hr_shadow(s, 0, Y, SW, SH, 40, 30, -8, 120);
    hr_fill(s, 0, Y, SW, SH - Y + 60, Y > 0 ? 40 : 0, pal.bg, 255);
    /* the list, under the header */
    Surf c = *s; surf_clip(&c, 0, Y + LIST_Y - 8, SW, SH - LIST_Y + 8);
    int sy = (int)scroll.x;
    for (int i = 0; i < nr; i++) {
        Row *r = &rows[i];
        int y = Y + LIST_Y + row_y(i) - sy, h = r->kind == R_HDR ? HDR_H : ROW_H;
        if (y > SH || y + h < Y + LIST_Y - 20) continue;
        if (r->kind == R_HDR) { ht(&c, HF_CAPS, 48, y + 28, pal.sub, 255, 3, r->label); continue; }
        int first = i == 0 || rows[i - 1].kind == R_HDR, last = i == nr - 1 || rows[i + 1].kind == R_HDR;
        float p = press_of(500 + i);
        /* the rows of a section are one card */
        int top = y + (first ? 0 : -20), bot = y + ROW_H - 8 + (last ? 0 : 20);
        hr_fill(&c, 32, top, SW - 64, bot - top, first || last ? 24 : 0, h_mix(pal.card, pal.track, p * 0.6f), 255);
        if (first) hr_fill(&c, 32, y, SW - 64, ROW_H - 8 + (last ? 0 : 20), 24, h_mix(pal.card, pal.track, p * 0.6f), 255);
        if (!last) hr_fill(&c, 56, y + ROW_H - 4, SW - 112, 2, 1, pal.line, 255);
        int my = y + (ROW_H - 8) / 2;
        ht(&c, HF_LABEL, 60, my - ht_h(HF_LABEL) / 2, pal.text, 255, 0, r->label);
        int vx = SW - 64 - (int)val_k[i].x;
        if (r->kind == R_BRI) {
            for (int j = 0; j < 5; j++) hr_fill(&c, vx - 5 * 62 + j * 62 + 8, my - 14, 52, 28, 10, j < bri_lv ? 0xE0A020 : pal.track, 255);
        } else if (r->kind == R_SFX || r->kind == R_MUSIC) {
            int on = r->kind == R_SFX ? cfg.sfx : cfg.music;
            hr_fill(&c, vx - 92, my - 24, 92, 48, 24, on ? accent_ink : pal.track, 255);
            hi_circle(&c, (float)(vx - (on ? 24 : 68)), (float)my, 19, 0, 0xffffff, 255);
        } else {
            char v[96]; value_text(r, v, sizeof v);
            int arrows = r->kind != R_ABOUT;
            int e = ht_right(&c, HF_BODYB, vx - (arrows ? 34 : 0), my - ht_h(HF_BODYB) / 2, i == foc ? pal.text : pal.sub, 255, 0, v);
            (void)e;
            if (arrows) icon(&c, IC_RIGHT, vx - 12.f, (float)my, 24, pal.faint, 255);
            if (r->kind == R_COLOR) hi_circle(&c, (float)(vx - 34 - ht_w(HF_BODYB, v, 0) - 28), (float)my, 14, 0, accent, 255);
        }
    }
    /* the header (over the list) */
    hr_fill(s, 0, Y, SW, LIST_Y - 10, Y > 0 ? 40 : 0, pal.bg, 255);
    float p = press_of(499); int d = (int)(p * 5);
    card(s, 32 + d, Y + 24 + d, 210 - 2 * d, 84 - 2 * d, 26, (int)(5 - 4 * p));
    icon(s, IC_BACK, 70.f, Y + 66.f, 30, pal.text, 255);
    ht(s, HF_LABEL, 94, Y + 66 - ht_h(HF_LABEL) / 2, pal.text, 255, 0, "Back");
    key_cap(s, 32 + 210 - 38, Y + 66, 18, "B", pal.chip, pal.sub);
    ht(s, HF_TITLE, 270, Y + 66 - ht_h(HF_TITLE) / 2, pal.text, 255, 0, "Settings");
    /* a scroll bar while it moves */
    int lh = list_h(), vh = SH - LIST_Y;
    if (lh > vh && (spr_moving(&scroll, .5f) || dragging)) {
        int bh = vh * vh / lh, by = LIST_Y + (int)((vh - bh) * scroll.x / (lh - vh));
        hr_fill(s, SW - 14, Y + by, 6, bh, 3, pal.faint, 200);
    }
}

/* ---------------------------------------------------------------- moving */
static void keep_in_view(void) {
    int vh = SH - LIST_Y - 48, y = row_y(foc), h = ROW_H;
    if (foc > 0 && rows[foc - 1].kind == R_HDR) { y -= HDR_H; h += HDR_H; }
    float to = scroll.to;
    if (y < to) to = (float)y;
    if (y + h > to + vh) to = (float)(y + h - vh);
    scroll.to = clampf(to, 0, (float)MAX(0, list_h() - vh));
}
void set_update(float dt) {
    if (spr_moving(&scroll, .3f) && !dragging) { spr_step(&scroll, K_SNAP, C_SNAP, dt); dirty(1); }
    for (int i = 0; i < nr; i++) if (spr_moving(&val_k[i], .2f)) { spr_step(&val_k[i], 700, 26, dt); dirty(1); }
    if (pressed(B_B) || pressed(B_SELECT) || pressed(B_START)) { press_hit(499); go_page(P_HOME); return; }
    int was = foc;
    if (repeat(B_UP)) { int f = foc - 1; while (f >= 0 && rows[f].kind == R_HDR) f--; if (f >= 0) foc = f; else sfx(SFX_BUMP); }
    if (repeat(B_DOWN)) { int f = foc + 1; while (f < nr && rows[f].kind == R_HDR) f++; if (f < nr) foc = f; else sfx(SFX_BUMP); }
    if (repeat(B_L1)) { int f = foc - 1; while (f > 0 && rows[f].kind != R_HDR) f--; f--; while (f > 0 && rows[f - 1].kind != R_HDR) f--; if (f >= 1) foc = f; }
    if (repeat(B_R1)) { int f = foc; while (f < nr && rows[f].kind != R_HDR) f++; if (f + 1 < nr) foc = f + 1; }
    if (foc != was) { sfx(SFX_CURSOR); keep_in_view(); dirty(0); dirty(1); }
    if (repeat(B_LEFT)) change(foc, -1);
    if (repeat(B_RIGHT) || pressed(B_A)) { if (pressed(B_A)) press_hit(500 + foc); change(foc, 1); }

    /* the finger: drag the list, tap a row */
    press_touch(499, 32, 24, 210, 84);
    if (tapped(32, 24, 210, 84)) { go_page(P_HOME); return; }
    if (tch.down && tch.moved && !dragging && tch.y0 > LIST_Y && abs(tch.y - tch.y0) > abs(tch.x - tch.x0)) { dragging = 1; drag_base = scroll.x; }
    int vh = SH - LIST_Y - 24;
    if (dragging) {
        if (tch.down) {
            float y = drag_base - (tch.y - tch.y0), mx = (float)MAX(0, list_h() - vh);
            if (y < 0) y *= 0.35f;
            if (y > mx) y = mx + (y - mx) * 0.35f;
            scroll.x = scroll.to = y; scroll.v = 0; dirty(1);
        } else {
            dragging = 0;
            scroll.to = clampf(scroll.x, 0, (float)MAX(0, list_h() - vh));
        }
        return;
    }
    for (int i = 0; i < nr; i++) {
        if (rows[i].kind == R_HDR) continue;
        int y = LIST_Y + row_y(i) - (int)scroll.x;
        if (y + ROW_H < LIST_Y || y > SH) continue;
        press_touch(500 + i, 32, y, SW - 64, ROW_H - 8);
        if (tapped(32, MAX(y, LIST_Y), SW - 64, ROW_H - 8)) {
            if (foc != i) { foc = i; dirty(0); }
            if (rows[i].kind == R_BRI && tch.x > SW - 64 - 5 * 62) {   /* a tap on the bar: that level */
                int lv = CLAMP((tch.x - (SW - 64 - 5 * 62)) / 62 + 1, 1, 5);
                if (lv != bri_lv) change(i, lv - bri_lv);
            } else change(i, 1);
            return;
        }
    }
    int y = LIST_Y + row_y(foc) - (int)scroll.x;
    if (y >= LIST_Y - 10 && y < SH) focus_to(32, y, SW - 64, ROW_H - 8, 24);
}
