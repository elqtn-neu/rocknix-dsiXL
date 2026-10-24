// config.c: config.ini, state.ini, the battery and the clock.
#define _GNU_SOURCE
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <glob.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/time.h>

Config cfg;

static void mkdirs(const char *path) {
    char tmp[512]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

static void defaults(void) {
    memset(&cfg, 0, sizeof cfg);
    snprintf(cfg.user, sizeof cfg.user, "User");
    cfg.color = 0;
    cfg.clock24 = 1; cfg.sfx = 1; cfg.music = 1; cfg.rumble = 1; cfg.lcd = 1; cfg.boot_splash = 1; cfg.boot_wait = 0;
    cfg.dim_after = 120; cfg.screen_off_after = 300;
    struct stat st;
    const char *pl = getenv("DSIXL_PLATFORM");
    cfg.banana = pl ? !strcmp(pl, "banana") : stat("/etc/banana/systems.conf", &st) == 0;
    if (cfg.banana) {
        /* BananaOS: the card's data partition is /data, the games in roms/<system> */
        const char *d = getenv("DSIXL_DATA"); if (!d || !*d) d = "/data";      /* (tests: somewhere else) */
        snprintf(cfg.dir_nds, sizeof cfg.dir_nds, "%s/roms/nds", d);
        snprintf(cfg.dir_gba, sizeof cfg.dir_gba, "%s/roms/gba", d);
        snprintf(cfg.media, sizeof cfg.media, "%s/dsixl/media", d);
        snprintf(cfg.sounds, sizeof cfg.sounds, "%s/dsixl/sounds", d);
        return;
    }
    int dev = stat("/storage/roms", &st) == 0;
    snprintf(cfg.dir_nds, sizeof cfg.dir_nds, "%s", dev ? "/storage/roms/nds" : "roms/nds");
    snprintf(cfg.dir_gba, sizeof cfg.dir_gba, "%s", dev ? "/storage/roms/gba" : "roms/gba");
    /* media and sounds on the games share, so they can be dropped in over the network */
    snprintf(cfg.media, sizeof cfg.media, "%s", dev ? "/storage/roms/dsixl/media" : "media");
    snprintf(cfg.sounds, sizeof cfg.sounds, "%s", dev ? "/storage/roms/dsixl/sounds" : "sounds");
}

static void find_home(void) {
    const char *e = getenv("DSIXL_HOME");
    struct stat st;
    if (e && *e) snprintf(cfg.home, sizeof cfg.home, "%s", e);
    else if (stat("/storage/.config/dsixl", &st) == 0) snprintf(cfg.home, sizeof cfg.home, "/storage/.config/dsixl");
    else if (stat("/data/dsixl/dsixl", &st) == 0) snprintf(cfg.home, sizeof cfg.home, "/data/dsixl");
    else {
        char exe[PATH_MAX]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) {
            exe[n] = 0; char *sl = strrchr(exe, '/'); if (sl) *sl = 0;
            /* build/dsixl: the checkout is one up */
            char a[PATH_MAX + 16]; snprintf(a, sizeof a, "%s/assets", exe);
            if (stat(a, &st) != 0) { sl = strrchr(exe, '/'); if (sl) *sl = 0; }
            snprintf(cfg.home, sizeof cfg.home, "%s", exe);
        } else snprintf(cfg.home, sizeof cfg.home, ".");
    }
    snprintf(cfg.cache, sizeof cfg.cache, "%s/cache", cfg.home);
    mkdirs(cfg.cache);
}

static char *trim(char *s) {
    while (isspace((uint8_t)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((uint8_t)e[-1])) *--e = 0;
    return s;
}

static void read_ini(const char *path, void (*kv)(const char *, const char *)) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';' || *s == '[') continue;
        char *eq = strchr(s, '='); if (!eq) continue;
        *eq = 0;
        kv(trim(s), trim(eq + 1));
    }
    fclose(f);
}

#define STR(k, field) if (!strcmp(key, k)) { snprintf(cfg.field, sizeof cfg.field, "%s", v); return; }
#define INT(k, field) if (!strcmp(key, k)) { cfg.field = atoi(v); return; }
static void cfg_kv(const char *key, const char *v) {
    STR("user", user) INT("color", color) INT("clock24", clock24) INT("sound_effects", sfx) INT("music", music) INT("vibration", rumble) INT("dsi_screen", lcd) INT("theme", theme)
    INT("boot_splash", boot_splash) INT("boot_wait", boot_wait) INT("swap_ab", swap_ab)
    INT("dim_after", dim_after) INT("screen_off_after", screen_off_after)
    if (!strcmp(key, "birthday")) { if (sscanf(v, "%d-%d", &cfg.bday_month, &cfg.bday_day) != 2) cfg.bday_month = cfg.bday_day = 0; return; }
    STR("nds_roms", dir_nds) STR("gba_roms", dir_gba) STR("media", media) STR("sounds", sounds)
}
static void state_kv(const char *key, const char *v) {
    INT("system", sys) STR("last_nds", last[0]) STR("last_gba", last[1]) INT("top_view", top_view) STR("last_fav", last_fav)
    INT("recent_system", recent_sys) STR("recent", recent)
    INT("sort", sort) INT("show_hidden", show_hidden) INT("seen_help_nds", seen_help[0]) INT("seen_help_gba", seen_help[1])
}

void config_load(void) {
    defaults();
    find_home();
    char p[400];
    snprintf(p, sizeof p, "%s/config.ini", cfg.home);
    if (!file_exists_cfg(p)) config_save();
    read_ini(p, cfg_kv);
    snprintf(p, sizeof p, "%s/state.ini", cfg.home);
    read_ini(p, state_kv);
    cfg.color = CLAMP(cfg.color, 0, 15);
    cfg.sys = CLAMP(cfg.sys, 0, 2);
    cfg.recent_sys = CLAMP(cfg.recent_sys, 0, 1);
    cfg.sort = CLAMP(cfg.sort, 0, 2);
}

int file_exists_cfg(const char *p) { struct stat st; return stat(p, &st) == 0; }

static void write_atomic(const char *path, const char *text) {
    char tmp[420]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fputs(text, f);
    if (fclose(f) == 0) rename(tmp, path);
}

void config_save(void) {
    char p[400], buf[3000], bday[8] = "";
    if (cfg.bday_month && cfg.bday_day) snprintf(bday, sizeof bday, "%02d-%02d", cfg.bday_month, cfg.bday_day);
    snprintf(p, sizeof p, "%s/config.ini", cfg.home);
    snprintf(buf, sizeof buf,
        "# DSi Menu for ROCKNIXDS. Edit freely; the menu rewrites this file when you change a setting in it.\n"
        "user = %s\n"
        "# 0 grey, 1 brown, 2 red, 3 pink, 4 orange, 5 yellow, 6 lime, 7 green, 8 dark green, 9 sea green,\n"
        "# 10 turquoise, 11 blue, 12 dark blue, 13 purple, 14 violet, 15 magenta\n"
        "color = %d\n"
        "clock24 = %d\n"
        "sound_effects = %d\n"
        "music = %d\n"
        "vibration = %d\n"
        "# the screens: 1 like a DSi's (each pixel with a fine gap and colour stripes), 2 that with the DS screen's own\n"
        "# (duller) colours, 0 plain square pixels\n"
        "dsi_screen = %d\n"
        "# the look: 0 by the clock (night from 19:00 to 7:00), 1 light, 2 night\n"
        "theme = %d\n"
        "# the health and safety screen at start (1), and how it ends: 0 waits for a touch, n continues after n seconds\n"
        "boot_splash = %d\n"
        "boot_wait = %d\n"
        "# 1: confirm with the bottom face button instead of the right one\n"
        "swap_ab = %d\n"
        "# saving the battery in the menu: the screens dim after this many seconds untouched, then go off (0: never)\n"
        "dim_after = %d\n"
        "screen_off_after = %d\n"
        "# your birthday, MM-DD (a cake and confetti on the day)\n"
        "birthday = %s\n"
        "nds_roms = %s\n"
        "gba_roms = %s\n"
        "# art: <media>/<nds|gba>/<box|label|logo>/<rom name without extension>.png (tools/dsixl-scrape.py fills it)\n"
        "media = %s\n"
        "# your own sound pack: see the README (boot.wav, cursor.wav, select.wav, ..., music.mp3)\n"
        "sounds = %s\n",
        cfg.user, cfg.color, cfg.clock24, cfg.sfx, cfg.music, cfg.rumble, cfg.lcd, cfg.theme, cfg.boot_splash, cfg.boot_wait, cfg.swap_ab, cfg.dim_after, cfg.screen_off_after, bday,
        cfg.dir_nds, cfg.dir_gba, cfg.media, cfg.sounds);
    write_atomic(p, buf);
}

void state_save(void) {
    char p[400], buf[1600];
    snprintf(p, sizeof p, "%s/state.ini", cfg.home);
    snprintf(buf, sizeof buf, "system = %d\nlast_nds = %s\nlast_gba = %s\nlast_fav = %s\ntop_view = %d\nrecent_system = %d\nrecent = %s\n"
             "sort = %d\nshow_hidden = %d\nseen_help_nds = %d\nseen_help_gba = %d\n",
             cfg.sys, cfg.last[0], cfg.last[1], cfg.last_fav, cfg.top_view, cfg.recent_sys, cfg.recent,
             cfg.sort, cfg.show_hidden, cfg.seen_help[0], cfg.seen_help[1]);
    write_atomic(p, buf);
}

static const struct { const char *name; uint8_t r, g, b; } FAV[16] = {
    /* the DS firmware's palette (RGB15 values scaled to 8 bits) */
    { "Grey", 97, 130, 154 }, { "Brown", 186, 73, 0 }, { "Red", 251, 0, 24 }, { "Pink", 251, 138, 251 },
    { "Orange", 251, 146, 0 }, { "Yellow", 243, 227, 0 }, { "Lime", 170, 251, 0 }, { "Green", 0, 251, 0 },
    { "Dark Green", 0, 162, 56 }, { "Sea Green", 73, 219, 138 }, { "Turquoise", 48, 186, 243 }, { "Blue", 0, 89, 243 },
    { "Dark Blue", 0, 0, 146 }, { "Purple", 138, 0, 211 }, { "Violet", 211, 0, 235 }, { "Magenta", 251, 0, 146 },
};
uint32_t fav_color(int i) { i = CLAMP(i, 0, 15); return ((uint32_t)FAV[i].r << 16) | ((uint32_t)FAV[i].g << 8) | FAV[i].b; }
const char *fav_color_name(int i) { return FAV[CLAMP(i, 0, 15)].name; }

int battery_level(int *charging) {
    static double last_t = -10; static int lvl = -1, chg = 0;
    struct timeval tv; gettimeofday(&tv, 0);
    double t = tv.tv_sec + tv.tv_usec * 1e-6;
    if (t - last_t > 5) {                 /* sysfs once every few seconds is plenty */
        last_t = t; lvl = -1; chg = 0;
        glob_t g;
        if (!glob("/sys/class/power_supply/*/type", 0, 0, &g)) {
            for (size_t i = 0; i < g.gl_pathc && lvl < 0; i++) {
                char typ[32] = "", p[300]; FILE *f = fopen(g.gl_pathv[i], "r");
                if (!f) continue;
                if (!fgets(typ, sizeof typ, f)) typ[0] = 0;
                fclose(f);
                if (strncmp(typ, "Battery", 7)) continue;
                snprintf(p, sizeof p, "%s", g.gl_pathv[i]); *strrchr(p, '/') = 0;
                char q[340]; snprintf(q, sizeof q, "%s/capacity", p);
                if ((f = fopen(q, "r"))) { if (fscanf(f, "%d", &lvl) != 1) lvl = -1; fclose(f); }
                snprintf(q, sizeof q, "%s/status", p);
                if ((f = fopen(q, "r"))) { char st[32] = ""; if (fgets(st, sizeof st, f)) chg = !strncmp(st, "Charging", 8) || !strncmp(st, "Full", 4); fclose(f); }
            }
            globfree(&g);
        }
        const char *fake = getenv("DSIXL_FAKE_BATTERY");
        if (fake) lvl = atoi(fake);
    }
    if (charging) *charging = chg;
    return lvl;
}

static struct tm now_tm(void) {
    struct timeval tv; gettimeofday(&tv, 0);
    time_t t = tv.tv_sec;
    const char *fake = getenv("DSIXL_FAKE_TIME");
    if (fake) t = (time_t)atoll(fake);
    struct tm lt; localtime_r(&t, &lt);
    return lt;
}
int is_birthday(void) { struct tm t = now_tm(); return cfg.bday_month && t.tm_mon + 1 == cfg.bday_month && t.tm_mday == cfg.bday_day; }
int is_snow_season(void) { struct tm t = now_tm(); return t.tm_mon == 11; }

void clock_text(char *date, int dsz, char *tm_, int tsz, int *colon_on) {
    struct timeval tv; gettimeofday(&tv, 0);
    time_t t = tv.tv_sec;
    const char *fake = getenv("DSIXL_FAKE_TIME");     /* screenshots: a fixed time */
    if (fake) t = (time_t)atoll(fake);
    struct tm lt; localtime_r(&t, &lt);
    snprintf(date, (size_t)dsz, "%02d/%02d", lt.tm_mon + 1, lt.tm_mday);
    int h = lt.tm_hour;
    if (!cfg.clock24) { h %= 12; if (!h) h = 12; }
    snprintf(tm_, (size_t)tsz, "%02d %02d", h, lt.tm_min);   /* the DSi blinks the colon: drawn apart */
    if (colon_on) *colon_on = fake ? 1 : tv.tv_usec < 500000;
}
