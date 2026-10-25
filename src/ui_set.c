// ui_set.c: Settings (the gear, SELECT, or START > Settings). The top screen: the time, big, and what the setting
// in focus does; the bottom one the settings, a list: left / right (or a tap) changes one, at once, where BananaOS
// keeps it (its own Settings reads the same files).
#define _GNU_SOURCE
#include "ui_int.h"
#include "gamecfg.h"
#include "power.h"
#include "job.h"
#include "dsi.h"
#include "plat/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { R_HDR, R_EMU, R_BRI, R_THEME, R_COLOR, R_SFX, R_MUSIC, R_RUMBLE, R_SORT, R_ART, R_CLOCK, R_DIM, R_OFF, R_ABOUT, R_LCD };
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
static const char *LCD_N[] = { "Plain pixels", "DSi LCD", "DSi LCD + DS colors" };

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
    add(R_LCD, 0, 0, "Menu screen look", "How this menu looks on the panels: plain pixels, or a DSi's LCD with its fine grid "
                                         "and colour stripes (and, with DS colors, a DS screen's softer colours).");
    add(R_BRI, 0, 0, "Brightness", "How bright both screens are. The sun on the home screen steps through it too.");
    add(R_THEME, 0, 0, "Theme", "By the clock: light in the day, night from seven in the evening to seven in the morning.");
    add(R_COLOR, 0, 0, "Your color", "Your DS favorite color: the menu's buttons, the focus, the highlights.");
    add(R_HDR, 0, 0, "SOUND", 0);
    add(R_SFX, 0, 0, "Sounds", "The menu's sounds: the ticks, the chimes, a game going in.");
    add(R_MUSIC, 0, 0, "Music", "The menu's tune, softly, while you choose.");
    add(R_RUMBLE, 0, 0, "Vibration", "A small buzz you feel: a game going in, the end of a shelf, a long press.");
    add(R_HDR, 0, 0, "SHELVES", 0);
    add(R_SORT, 0, 0, "Order", "How the DS and GBA shelves are sorted. A to Z keeps the letters to jump with.");
    add(R_ART, 0, 0, "Get box art", "Box art for every game that has none, from free game databases. Needs Wi-Fi. "
                                    "It goes on in the background: the covers appear as they arrive.");
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

/* ---------------------------------------------------------------- getting art (dsixl-scrape.py, in the background) */
static Job artjob; static long art_off; static int art_done, art_total, art_found, art_ran;
static char art_status[96] = "";
static float art_prog = -1;
static void art_job_start(void) {
    if (job_running(&artjob)) return;
    char sp[400]; snprintf(sp, sizeof sp, "%s/dsixl-scrape.py", cfg.home);
    if (access(sp, R_OK)) { notice("The art script is missing: install the menu again"); sfx(SFX_ERROR); return; }
    char cmd[900];
    snprintf(cmd, sizeof cmd, "command -v python3 >/dev/null || { echo '@\tnopython'; exit 3; }; "
                              "DSIXL_HOME='%s' exec python3 -u '%s' --progress --media '%s' --nds '%s' --gba '%s'",
             cfg.home, sp, cfg.media, cfg.dir_nds, cfg.dir_gba);
    if (job_start(&artjob, "art", cmd)) { notice("Couldn't start getting art"); sfx(SFX_ERROR); return; }
    art_off = 0; art_done = art_total = art_found = 0; art_ran = 1; art_prog = -1;
    snprintf(art_status, sizeof art_status, "Starting");
    sfx(SFX_OPEN);
}
void getart_poll(void) {
    static double t_;
    if (!art_ran || T - t_ < 0.25) return;
    t_ = T;
    int running = job_running(&artjob);
    FILE *f = fopen(artjob.out, "rb");
    if (f) {
        fseek(f, art_off, SEEK_SET);
        char line[700];
        while (fgets(line, sizeof line, f)) {
            if (!strchr(line, '\n') && running) break;              /* (a line still being written) */
            art_off = ftell(f);
            line[strcspn(line, "\n")] = 0;
            if (line[0] != '@') continue;
            char *fld[8]; int nf = 0;
            for (char *t = strtok(line, "\t"); t && nf < 8; t = strtok(0, "\t")) fld[nf++] = t;
            if (nf >= 2 && !strcmp(fld[1], "nopython")) snprintf(art_status, sizeof art_status, "Needs Python 3, which this BananaOS lacks");
            else if (nf >= 4 && !strcmp(fld[1], "download")) {
                snprintf(art_status, sizeof art_status, "Database: %s of %s MB", fld[2], fld[3]);
                art_prog = atof(fld[3]) > 0 ? (float)(atof(fld[2]) / atof(fld[3])) : -1;
            } else if (nf >= 2 && !strcmp(fld[1], "reading")) { snprintf(art_status, sizeof art_status, "Reading the database"); art_prog = -1; }
            else if (nf >= 3 && !strcmp(fld[1], "index")) { snprintf(art_status, sizeof art_status, "Looking up the art"); art_prog = -1; }
            else if (nf >= 7 && !strcmp(fld[1], "game")) {
                art_done = atoi(fld[3]); art_total = atoi(fld[4]);
                art_prog = art_total > 0 ? (float)art_done / art_total : -1;
                snprintf(art_status, sizeof art_status, "%d of %d", art_done, art_total);
                if (atoi(fld[6]) > 0) {
                    art_found++;
                    int sy = !strcmp(fld[2], "gba") ? SYS_GBA : SYS_NDS;
                    for (int i = 0; i < lib[sy].n; i++) if (!strcmp(lib[sy].g[i].base, fld[5])) art_forget(&lib[sy].g[i]);
                    home_selection_changed(); dirty(1);
                }
            }
        }
        fclose(f);
    }
    if (page == P_SETTINGS) { dirty(0); dirty(1); }
    if (!running) {
        art_ran = 0;
        if (artjob.rc == 0) {
            snprintf(art_status, sizeof art_status, art_found ? "Done: %d new" : "Done: nothing new", art_found);
            notice(art_found ? "Box art: done" : "Box art: nothing new found"); sfx(SFX_DONE);
        } else if (!strstr(art_status, "Python")) {
            snprintf(art_status, sizeof art_status, "Stopped. Is Wi-Fi on?");
            notice("Getting art stopped: is Wi-Fi on?"); sfx(SFX_ERROR);
        }
    }
}

/* ---------------------------------------------------------------- values */
static int dim_i(void) { int b = 0; for (int i = 0; i < 5; i++) if (DIM_S[i] == cfg.dim_after) b = i; return b; }
static int off_i(void) { int b = 0; for (int i = 0; i < 5; i++) if (OFF_S[i] == cfg.screen_off_after) b = i; return b; }
static void value_text(const Row *r, char *out, int n) {
    switch (r->kind) {
    case R_EMU: snprintf(out, (size_t)n, "%s", opts[r->sys][r->opt].names[choice[r->sys][r->opt]]); break;
    case R_THEME: snprintf(out, (size_t)n, "%s", THEME_N[CLAMP(cfg.theme, 0, 2)]); break;
    case R_LCD: snprintf(out, (size_t)n, "%s", LCD_N[CLAMP(cfg.lcd, 0, 2)]); break;
    case R_COLOR: snprintf(out, (size_t)n, "%s", fav_color_name(cfg.color)); break;
    case R_SFX: snprintf(out, (size_t)n, cfg.sfx ? "On" : "Off"); break;
    case R_MUSIC: snprintf(out, (size_t)n, cfg.music ? "On" : "Off"); break;
    case R_RUMBLE: snprintf(out, (size_t)n, cfg.rumble ? "On" : "Off"); break;
    case R_SORT: snprintf(out, (size_t)n, "%s", cfg.sort == 1 ? "Recently played" : cfg.sort == 2 ? "Most played" : "A to Z"); break;
    case R_CLOCK: snprintf(out, (size_t)n, clock24() ? "24-hour" : "12-hour"); break;
    case R_DIM: snprintf(out, (size_t)n, "%s", DIM_N[dim_i()]); break;
    case R_OFF: snprintf(out, (size_t)n, "%s", OFF_N[off_i()]); break;
    case R_ABOUT: snprintf(out, (size_t)n, "Version %s", DSIXL_VERSION); break;
    case R_ART: snprintf(out, (size_t)n, "%s", art_status[0] ? art_status : "Start"); break;
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
    case R_LCD: cfg.lcd = (cfg.lcd + d + 3) % 3; config_save(); plat_set_lcd(cfg.lcd); break;
    case R_COLOR: cfg.color = (cfg.color + d + 16) % 16; config_save(); theme_update(); break;
    case R_SFX: cfg.sfx = !cfg.sfx; config_save(); break;
    case R_MUSIC: cfg.music = !cfg.music; config_save(); if (cfg.music) music_play(); else music_fade(0, 0.4f); break;
    case R_RUMBLE: cfg.rumble = !cfg.rumble; config_save(); if (cfg.rumble) buzz(40, 0.5f); break;
    case R_SORT: cfg.sort = (cfg.sort + d + 3) % 3; state_save(); shelves_build(); home_selection_changed(); break;
    case R_ART: if (d < 0) return 0; art_job_start(); return 2;
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
static const char *section_of(int i) {
    while (i > 0 && rows[i].kind != R_HDR) i--;
    return rows[i].label;
}
void set_draw_top(Surf *s) {
    surf_noclip(s);
    if (pal.dark) dsi_bg_dark(s, accent, 0.3f, 0); else dsi_bg_top(s, 0, 0);
    uint32_t tx = pal.dark ? 0xdfe3e8 : D_TEXT, sub = pal.dark ? 0x9aa2ad : D_TEXT_SOFT;
    time_t t = time(0); struct tm lt; localtime_r(&t, &lt);
    if (getenv("DSIXL_FAKE_TIME")) { lt.tm_hour = atoi(getenv("DSIXL_FAKE_TIME")); lt.tm_min = 42; lt.tm_wday = 6; lt.tm_mday = 24; lt.tm_mon = 9; }
    char tm[16], date[64];
    if (clock24()) snprintf(tm, sizeof tm, "%d:%02d", lt.tm_hour, lt.tm_min);
    else snprintf(tm, sizeof tm, "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
    strftime(date, sizeof date, "%A, %e %B", &lt);
    /* the clock as the DSi's: big, its digits outlined white on the stripes */
    int cw = ht_w(HF_CLOCK, tm, -2), cx = (SW - cw) / 2 - (clock24() ? 0 : 40), cy = 64;
    for (int o = 0; o < 8; o++) {
        float an = o * 0.785f;
        ht(s, HF_CLOCK, cx + (int)(cosf(an) * 5), cy + (int)(sinf(an) * 5), pal.dark ? 0x101418 : 0xffffff, 255, -2, tm);
    }
    ht(s, HF_CLOCK, cx + 4, cy + 6, 0x000000, 40, -2, tm);
    int e = ht(s, HF_CLOCK, cx, cy, tx, 255, -2, tm);
    if (!clock24()) ht(s, HF_BD, e + 14, cy + ht_h(HF_CLOCK) - ht_h(HF_BD) - 26, sub, 255, 0, lt.tm_hour < 12 ? "AM" : "PM");
    ht_center(s, HF_MD, SW / 2, cy + ht_h(HF_CLOCK) - 4, sub, 255, 0, date[0] == ' ' ? date + 1 : date);
    /* what the focused one does, in a window titled by its section */
    if (foc < 0 || foc >= nr) return;
    Row *r = &rows[foc];
    int wx = 56, wy = 360, ww = SW - 112, wh = 360;
    dsi_window(s, wx, wy, ww, wh, section_of(foc), 255);
    int x = wx + 36, w = ww - 72, y = wy + 80 + 24;
    if (r->kind == R_COLOR) {
        hi_circle(s, wx + 36 + 56.f, y + 76.f, 58, 0, 0xffffff, 255);
        hi_circle(s, wx + 36 + 56.f, y + 76.f, 54, 0, accent, 255);
        hi_circle(s, wx + 36 + 40.f, y + 58.f, 16, 0, 0xffffff, 90);
        x += 140; w -= 140;
    }
    char v[96]; value_text(r, v, sizeof v);
    ht(s, HF_BD, x, y, D_TEXT, 255, 0, r->label);
    const char *help = r->help, *note = 0;
    if (r->kind == R_EMU) { const GameOpt *o = &opts[r->sys][r->opt]; help = o->help; note = o->notes[choice[r->sys][r->opt]]; }
    y += ht_h(HF_BD) + 10;
    if (help) y += 38 * ht_wrap(s, HF_XS, x, y, w, 38, 3, D_TEXT_SOFT, 255, help) + 10;
    if (note) {
        char b[200]; snprintf(b, sizeof b, "%s: %s", v, note);
        ht_wrap(s, HF_XS, x, y, w, 38, 2, D_TEXT, 255, b);
    }
    if (r->kind == R_ART && (art_ran || art_status[0])) {
        ht(s, HF_XS, x, y, D_TEXT, 255, 0, art_status);
        if (art_ran) {
            Accent a = dsi_accent();
            hr_fill(s, x, y + 46, w, 18, 9, 0xd8d8d8, 255);
            if (art_prog >= 0) hr_vgrad(s, x, y + 46, MAX(18, (int)(w * art_prog)), 18, 9, a.light, a.base, 255);
            else { int bw = w / 4, bx = x + (int)((w - bw) * (0.5f + 0.5f * sinf((float)T * 2.4f))); hr_vgrad(s, bx, y + 46, bw, 18, 9, a.light, a.base, 255); }
        }
    }
    if (r->kind == R_ABOUT) {
        char b[160]; float fg = 0, tg = 0;
        snprintf(b, sizeof b, "%d DS games, %d GBA games", lib[SYS_NDS].n, lib[SYS_GBA].n);
        ht(s, HF_XS, x, y, D_TEXT, 255, 0, b);
        if (!disk_free_gb(bpath("/data"), &fg, &tg)) { snprintf(b, sizeof b, "%.1f GB free of %.0f GB on the card", fg, tg); ht(s, HF_XS, x, y + 40, D_TEXT_SOFT, 255, 0, b); }
    }
}

/* ---------------------------------------------------------------- the bottom screen */
/* a value's arrows: little accent triangles, as the DSi's settings have */
static void tri(Surf *s, float cx, float cy, int dir, uint32_t c, int a) {
    float t[6] = { cx - dir * 9.f, cy - 13, cx - dir * 9.f, cy + 13, cx + dir * 11.f, cy };
    hi_poly(s, t, 3, c, a);
}
void set_draw_bot(Surf *s, float yoff, int alpha) {
    int Y = (int)yoff; (void)alpha;
    Surf bg = *s; if (Y > 0) surf_clip(&bg, 0, Y, SW, SH - Y);
    if (pal.dark) dsi_bg_dark(&bg, accent, 0.3f, 0); else dsi_bg_bottom(&bg, accent, 0.25f, 0);
    if (Y > 0) { rect_blend(s, 0, Y, SW, 6, 0xffffff, 160); rect_blend(s, 0, Y + 6, SW, 3, 0x000000, 30); }
    Accent ac = dsi_accent();
    /* the list, under the header */
    Surf c = *s; surf_clip(&c, 0, Y + LIST_Y - 8, SW, SH - LIST_Y + 8);
    int sy = (int)scroll.x;
    for (int i = 0; i < nr; i++) {
        Row *r = &rows[i];
        int y = Y + LIST_Y + row_y(i) - sy, h = r->kind == R_HDR ? HDR_H : ROW_H;
        if (r->kind == R_HDR) {
            /* the section: its heading, then one white panel holding its rows */
            int n = 0; while (i + 1 + n < nr && rows[i + 1 + n].kind != R_HDR) n++;
            int py = y + HDR_H, ph = n * ROW_H - 8;
            if (py > SH || py + ph < Y + LIST_Y - 20) { if (y > SH) break; continue; }
            if (y + h > Y + LIST_Y - 20) dsi_section(&c, 44, y + 18, SW - 88, r->label, 255);
            hr_shadow(&c, 32, py, SW - 64, ph, 22, 14, 6, pal.dark ? 160 : 60);
            hr_ring(&c, 30, py - 2, SW - 60, ph + 4, 24, 2, pal.dark ? 0x3a4048 : 0xb8b8b8, 255);
            hr_vgrad(&c, 32, py, SW - 64, ph, 22, pal.dark ? 0x2c3138 : 0xffffff, pal.dark ? 0x24282e : 0xf1f1f1, 255);
            continue;
        }
        if (y > SH || y + h < Y + LIST_Y - 20) continue;
        int last = i == nr - 1 || rows[i + 1].kind == R_HDR, first = rows[i - 1].kind == R_HDR;
        float p = press_of(500 + i);
        uint32_t tx = pal.dark ? 0xdfe3e8 : D_TEXT, sub = pal.dark ? 0x9aa2ad : D_TEXT_SOFT;
        if (i == foc || p > 0) {
            /* the focused row lit in the accent's palest, as the DSi marks the line it's on */
            int ry = y + (first ? 4 : 0), rh = ROW_H - 8 - (first ? 4 : 0) - (last ? 4 : 0) + (last ? 0 : 4);
            hr_vgrad(&c, 36, ry, SW - 72, rh, first || last ? 18 : 6, h_mix(ac.pale, 0xffffff, 0.3f), ac.pale, pal.dark ? 60 : 255);
            if (p > 0) hr_fill(&c, 36, ry, SW - 72, rh, 6, ac.base, (int)(p * 50));
        }
        if (!last) { rect_blend(&c, 56, y + ROW_H - 9, SW - 112, 2, 0xc8c8c8, pal.dark ? 80 : 255); rect_blend(&c, 56, y + ROW_H - 7, SW - 112, 2, 0xffffff, pal.dark ? 0 : 255); }
        int my = y + (ROW_H - 8) / 2 + (int)(p * 3);
        ht(&c, HF_MD, 64, my - ht_h(HF_MD) / 2, tx, 255, 0, r->label);
        int vx = SW - 64 - (int)val_k[i].x;
        if (r->kind == R_BRI) {
            for (int j = 0; j < 5; j++) {
                int bx = vx - 5 * 58 + j * 58 + 8, bh = 18 + j * 6;
                hr_fill(&c, bx - 1, my + 16 - bh - 1, 46, bh + 2, 6, j < bri_lv ? h_mix(ac.dark, 0, 0.1f) : 0xc0c0c0, 255);
                if (j < bri_lv) hr_vgrad(&c, bx, my + 16 - bh, 44, bh, 5, ac.light, ac.base, 255);
                else hr_fill(&c, bx, my + 16 - bh, 44, bh, 5, 0xe8e8e8, 255);
            }
        } else if (r->kind == R_SFX || r->kind == R_MUSIC || r->kind == R_RUMBLE) {
            /* On | Off: two buttons, the chosen one pressed in the accent */
            int on = r->kind == R_SFX ? cfg.sfx : r->kind == R_MUSIC ? cfg.music : cfg.rumble;
            for (int k = 0; k < 2; k++) {
                int bx = vx - 2 * 104 + k * 104 + 4, sel = k == 0 ? on : !on;
                if (sel) dsi_face_main(&c, bx, my - 28, 96, 56, 16, 0.6f, 255);
                else dsi_face(&c, bx, my - 28, 96, 56, 16, 0, 0, 255);
                ht_center(&c, HF_XS, bx + 48, my - ht_h(HF_XS) / 2 + (sel ? 2 : 0), sel ? 0xffffff : D_TEXT_SOFT, 255, 0, k ? "Off" : "On");
            }
        } else {
            char v[96]; value_text(r, v, sizeof v);
            int arrows = r->kind != R_ABOUT && r->kind != R_ART;
            int vw = ht_w(HF_XS, v, 0);
            if (r->kind == R_ART) {
                dsi_face_main(&c, vx - vw - 48, my - 28, vw + 48, 56, 16, p, 255);
                ht(&c, HF_XS, vx - vw - 24, my - ht_h(HF_XS) / 2 + (int)(p * 4), 0xffffff, 255, 0, v);
            } else {
                int x1 = vx - (arrows ? 30 : 0);
                ht_right(&c, HF_XS, x1, my - ht_h(HF_XS) / 2, i == foc ? tx : sub, 255, 0, v);
                if (arrows) {
                    int a = i == foc ? 255 : 110;
                    tri(&c, vx - 10.f, (float)my, 1, ac.base, a);
                    tri(&c, x1 - vw - 22.f, (float)my, -1, ac.base, a);
                }
                if (r->kind == R_COLOR) {
                    float ccx = x1 - vw - (arrows ? 60 : 30.f);
                    hi_circle(&c, ccx, (float)my, 17, 0, 0xffffff, 255);
                    hi_circle(&c, ccx, (float)my, 14, 0, accent, 255);
                }
            }
        }
    }
    /* the header (over the list): Back, the title */
    Surf hb = *s; surf_clip(&hb, 0, Y, SW, LIST_Y - 10);
    if (pal.dark) dsi_bg_dark(&hb, accent, 0.3f, 0); else dsi_bg_bottom(&hb, accent, 0.25f, 0);
    rect_blend(s, 0, Y + LIST_Y - 12, SW, 2, 0x000000, 30);
    rect_blend(s, 0, Y + LIST_Y - 10, SW, 2, 0xffffff, 200);
    float p = press_of(499);
    dsi_button(s, 32, Y + 24, 210, 84, "Back", "B", 0, p, 255);
    dsi_round(s, 300.f, Y + 66.f, 34, 0, 255);
    icon(s, IC_GEAR, 300.f, Y + 66.f, 40, ac.base, 255);
    ht(s, HF_BD, 350, Y + 66 - ht_h(HF_BD) / 2, pal.dark ? 0xdfe3e8 : D_TEXT, 255, 0, "Settings");
    /* the scroll bar: a thin accent rail with its thumb */
    int lh = list_h(), vh = SH - LIST_Y;
    if (lh > vh) {
        int bh = MAX(60, vh * vh / lh), by = LIST_Y + (int)((vh - bh) * clampf(scroll.x / (lh - vh), 0, 1));
        hr_fill(s, SW - 18, Y + LIST_Y + 4, 8, vh - 8, 4, 0x000000, 30);
        hr_vgrad(s, SW - 20, Y + by, 12, bh, 6, ac.light, ac.base, spr_moving(&scroll, .5f) || dragging ? 255 : 170);
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
    (void)y;
}
