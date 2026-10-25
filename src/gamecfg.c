// gamecfg.c: see gamecfg.h. The recommended choices are the balance of looks, speed and battery that ROCKNIXDS
// measured on the RG DS Plus (its dsflip/device/session.sh), and the plain good defaults for the GBA.
#define _GNU_SOURCE
#include "gamecfg.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

static const GameOpt NDS_OPTS[] = {
    { "Screen", "shader", "How the DS's pixels are drawn on the big screens.", 7, 0,
      { "Crisp pixels", "DSi LCD", "DSi LCD + DS colors", "Pixel grid", "Crisp, DS colors", "Smooth", "Pixel-perfect" },
      { "ds-crisp", "dsi-lcd", "dsi-lcd-color", "ds-grid", "ds-crisp-color", "none", "ds-integer" },
      { "Sharp, exactly 4x on this screen. Barely uses the GPU.",
        "Like the menu: the DSi screen's stripes and fine gaps.",
        "The DSi LCD with the DS screen's softer colors.",
        "Sharp, with the DS's faint pixel grid.",
        "Sharp, with the DS screen's softer colors.", "Softly smoothed. Uses no GPU at all: the most battery.",
        "Pixel-perfect squares, nothing else." } },
    { "Performance", "power_profile", "How hard the handheld works: speed against battery and heat.", 3, 1,
      { "Battery saver", "Balanced", "Max performance" }, { "battery", "balanced", "performance" },
      { "Lower clocks: the longest play. Heavy 3D games may slow down.",
        "Full speed in almost every game, with good battery life.",
        "Highest clocks, lowest input lag. Warmer, shorter battery." } },
    { "3D quality", "resolution3d", "The resolution 3D games are drawn at.", 2, 0,
      { "Standard (2x)", "High (3x)" }, { "2x", "3x" },
      { "Twice the DS's resolution: clean and full speed.",
        "Smoother 3D edges. Needs Max performance in heavy games." } },
    { "3D textures", "texture_filter", "How 3D surfaces are filtered.", 3, 0,
      { "DS original", "Smooth", "Sharp smooth" }, { "nearest", "bilinear", "sharp" },
      { "Exactly as the DS showed them.", "Softer textures, less blocky.", "Softer, but keeps edges crisp." } },
    { "3D engine", "renderer", "What draws the 3D.", 2, 0,
      { "Gengis Engine", "DraStic" }, { "superdrastic", "drastic" },
      { "ROCKNIXDS's renderer: the same picture, less CPU. Allows 3x.", "DraStic's own renderer." } },
    { "Resume", "resume_on_quit", "Leaving with the exit hotkey can save exactly where you are.", 2, 0,
      { "On", "Off" }, { "1", "0" },
      { "The exit hotkey saves, and the next start carries on from there.", "The game starts fresh each time." } },
};

static const GameOpt GBA_OPTS[] = {
    { "Emulator", "core", "Which GBA emulator runs the games.", 2, 0,
      { "mGBA", "gpSP" }, { "mgba", "gpsp" },
      { "The most accurate: every game runs right.", "Lighter on the battery. A few games have glitches." } },
    { "Screen size", "integerscale", "How the GBA's screen fills the top screen.", 2, 0,
      { "Pixel-perfect", "Fill screen" }, { "1", "0" },
      { "Exactly 4x: every pixel the same size. Thin borders.", "As big as it goes. Pixels slightly uneven." } },
    { "Screen", "smooth", "How the pixels are drawn.", 2, 0,
      { "Sharp", "Smooth" }, { "0", "1" },
      { "Crisp pixels, as the GBA showed them.", "Softly smoothed." } },
    { "Resume", "autosave", "Save exactly where you are when you leave.", 2, 0,
      { "On", "Off" }, { "1", "0" },
      { "Leaving saves a state; the next start loads it.", "The game starts fresh each time." } },
    { "Rewind", "rewind", "Hold a button to step back in time.", 2, 0,
      { "Off", "On" }, { "0", "1" },
      { "No rewind: a little less work for the handheld.", "Rewind with RetroArch's hotkey." } },
    { "Response", "runahead", "Cuts input lag by running a frame ahead.", 2, 0,
      { "Normal", "Quick" }, { "0", "1" },
      { "As the GBA itself responded.", "One frame less lag. Uses more CPU and battery." } },
};

/* ---- BananaOS: each setting where its own Settings screen keeps it (ui/src/settings.cpp), so the two agree ----
 * A setting may go to two files (its value there in vals2, or the same): the DS's are SuperDrastic's and
 * BananaDS's, whichever of the two plays. */
enum { KV_SPACED, KV_TIGHT, KV_QUOTED };       /* key = value (settings.conf, drastic.cfg); KEY=value; key = "value" */
typedef struct { const char *file, *key; int style; const char *vals[GOPT_MAX]; } BTarget;
typedef struct { GameOpt o; BTarget t[2]; } BOpt;
#define BD "/data/banana/"
static const BOpt B_NDS[] = {
    { { "Emulator", "nds_emulator", "Which DS emulator plays the games (BananaOS's Settings > Nintendo DS).", 2, 0,
        { "SuperDrastic", "BananaDS" }, { "superdrastic", "bananads" },
        { "DraStic on both screens, with its own clock governor.", "BananaOS's own (melonDS's core)." } },
      { { BD "settings.conf", "nds_emulator", KV_SPACED } } },
    { { "Screen", "shader", "How the DS's pixels are drawn on the big screens.", 6, 0,
        { "DSi LCD", "DSi LCD + DS colors", "Crisp pixels", "Crisp, DS colors", "Pixel grid", "Smooth" },
        { "dsi-lcd", "dsi-lcd-color", "ds-crisp", "ds-crisp-color", "ds-grid", "none" },
        { "Like the menu: the DSi screen's stripes and fine gaps.", "The DSi LCD with the DS screen's softer colors.",
          "Sharp, exactly 4x on this screen. Barely uses the GPU.", "Sharp, with the DS screen's softer colors.",
          "Sharp, with the DS's faint pixel grid.", "Softly smoothed. Uses no GPU at all." } },
      { { BD "superdrastic.conf", "DSFLIP_SHADER", KV_TIGHT }, { "/data/bananads/bananads.conf", "kms.shader", KV_SPACED } } },
    { { "3D quality", "hires_3d", "The resolution 3D games are drawn at.", 2, 0,
        { "Standard (2x)", "DS's own (1x)" }, { "1", "0" },
        { "Twice the DS's resolution: clean and full speed.", "The DS's own: the lightest." } },
      { { "/data/drastic/config/drastic.cfg", "hires_3d", KV_SPACED }, { "/data/bananads/bananads.conf", "render.scale", KV_SPACED, { "2", "1" } } } },
    { { "CPU clock", "cpugov", "How fast the handheld runs a game.", 2, 0,
        { "Saves battery", "Always full" }, { "1", "0" },
        { "The lowest clock each game runs at full speed, remembered.", "The top clock all the time." } },
      { { BD "superdrastic.conf", "DSFLIP_CPUGOV", KV_TIGHT }, { "/data/bananads/bananads.conf", "cpugov", KV_SPACED, { "on", "off" } } } },
    { { "Resume", "quick_resume", "Quitting can keep exactly where you are.", 2, 0,
        { "On", "Off" }, { "on", "off" },
        { "Quitting saves your place; the next start carries on from there.", "The game starts fresh each time." } },
      { { BD "settings.conf", "quick_resume", KV_SPACED } } },
};
static const BOpt B_GBA[] = {
    { { "Emulator", "core.gba", "Which GBA emulator runs the games.", 2, 1,
        { "mGBA", "gpSP" }, { "mgba", "gpsp" },
        { "The most accurate (if BananaOS has it).", "The fastest, lightest on the battery." } },
      { { BD "settings.conf", "core.gba", KV_SPACED } } },
    { { "Screen", "ra_shader", "How the GBA's pixels are drawn.", 3, 0,
        { "Sharp pixels", "LCD grid", "Smooth" }, { "sharp", "lcd", "off" },
        { "Crisp pixels at any size.", "A handheld's LCD grid.", "As RetroArch scales it." } },
      { { BD "settings.conf", "ra_shader", KV_SPACED } } },
    { { "Resume", "savestate_auto_save", "Save exactly where you are when you leave.", 2, 0,
        { "On", "Off" }, { "true", "false" },
        { "Leaving saves a state; the next start loads it.", "The game starts fresh each time." } },
      { { "/data/retroarch/retroarch.cfg", "savestate_auto_save", KV_QUOTED } } },
};
static GameOpt b_nds[ARRAY_LEN(B_NDS)], b_gba[ARRAY_LEN(B_GBA)];

int gamecfg_opts(int sys, const GameOpt **opts) {
    if (cfg.banana) {
        if (!b_nds[0].label) {
            for (int i = 0; i < ARRAY_LEN(B_NDS); i++) b_nds[i] = B_NDS[i].o;
            for (int i = 0; i < ARRAY_LEN(B_GBA); i++) b_gba[i] = B_GBA[i].o;
        }
        if (sys == SYS_NDS) { *opts = b_nds; return ARRAY_LEN(B_NDS); }
        *opts = b_gba; return ARRAY_LEN(B_GBA);
    }
    if (sys == SYS_NDS) { *opts = NDS_OPTS; return ARRAY_LEN(NDS_OPTS); }
    *opts = GBA_OPTS; return ARRAY_LEN(GBA_OPTS);
}

static const char *cfg_path(void) {
    const char *e = getenv("DSIXL_SYSTEM_CFG");
    return e && *e ? e : "/storage/.config/system/configs/system.cfg";
}
static const char *plat(int sys) { return sys == SYS_NDS ? "nds" : "gba"; }

static char *slurp(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb"); if (!f) { *len = 0; return 0; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)MAX(0, n) + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = 0; }
    fclose(f);
    if (b) b[n] = 0;
    *len = b ? (size_t)n : 0;
    return b;
}

/* the value of a whole key in the file, or NULL (into out) */
static int find_key(const char *text, const char *key, char *out, int sz) {
    size_t kl = strlen(key);
    for (const char *p = text; p && *p; ) {
        const char *e = strchr(p, '\n');
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1; int n = (int)((e ? e : v + strlen(v)) - v);
            while (n > 0 && (v[n - 1] == '\r' || v[n - 1] == ' ')) n--;
            snprintf(out, (size_t)sz, "%.*s", n, v);
            return 1;
        }
        p = e ? e + 1 : 0;
    }
    return 0;
}

/* BananaOS's /data, or (tests) DSIXL_DATA in its place */
static const char *bpath(const char *p) {
    static char b[4][400]; static int k;
    const char *d = getenv("DSIXL_DATA");
    if (!d || !*d || strncmp(p, "/data/", 6)) return p;
    k = (k + 1) % 4; snprintf(b[k], sizeof b[k], "%s%s", d, p + 5);
    return b[k];
}
/* "key = value" files: the value of key (its last line), quotes off; 1 found */
static int kv_get(const char *path, const char *key, char *out, int sz) {
    path = bpath(path);
    FILE *f = fopen(path, "r"); if (!f) return 0;
    char ln[512]; int found = 0; size_t kl = strlen(key);
    while (fgets(ln, sizeof ln, f)) {
        char *p = ln; while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || strncmp(p, key, kl)) continue;
        char *q = p + kl; while (*q == ' ' || *q == '\t') q++;
        if (*q != '=') continue;
        q++; while (*q == ' ' || *q == '\t') q++;
        int n = (int)strcspn(q, "\r\n"); while (n > 0 && (q[n - 1] == ' ' || q[n - 1] == '\t')) n--;
        if (n >= 2 && (q[0] == '"' || q[0] == '\'') && q[n - 1] == q[0]) { q++; n -= 2; }
        snprintf(out, (size_t)sz, "%.*s", n, q); found = 1;
    }
    fclose(f);
    return found;
}
/* sets key in the file (its line replaced, a second one dropped, else added), as BananaOS's setConfValue does */
static int kv_set(const char *path, const char *key, const char *val, int style) {
    path = bpath(path);
    char dir[300]; snprintf(dir, sizeof dir, "%s", path); char *sl = strrchr(dir, '/');
    if (sl) { *sl = 0; char cmd[400]; snprintf(cmd, sizeof cmd, "mkdir -p '%s'", dir); if (access(dir, F_OK) && system(cmd)) {} }
    /* the programs' own defaults first, as BananaOS's Settings does: a file with only our line would lose them */
    if (access(path, F_OK)) {
        if (strstr(path, "drastic.cfg")) { if (system("cp -n /usr/share/drastic/config/drastic.cfg /data/drastic/config/ 2>/dev/null")) {} }
        else if (strstr(path, "retroarch.cfg")) { if (system("cp -n /etc/banana/retroarch.cfg /data/retroarch/ 2>/dev/null")) {} }
    }
    size_t len; char *text = slurp(path, &len);
    if (!text && !access(path, F_OK)) return -1;        /* there but unreadable: left alone */
    char tmp[400]; snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(tmp, "w"); if (!f) { free(text); return -1; }
    char line[600];
    if (style == KV_TIGHT) snprintf(line, sizeof line, "%s=%s", key, val);
    else if (style == KV_QUOTED) snprintf(line, sizeof line, "%s = \"%s\"", key, val);
    else snprintf(line, sizeof line, "%s = %s", key, val);
    int done = 0; size_t kl = strlen(key);
    for (const char *p = text; p && *p; ) {
        const char *e = strchr(p, '\n'); int ll = (int)((e ? e : p + strlen(p)) - p);
        const char *q = p; while (*q == ' ' || *q == '\t') q++;
        int mine = *q != '#' && !strncmp(q, key, kl);
        if (mine) { const char *r = q + kl; while (*r == ' ' || *r == '\t') r++; mine = *r == '='; }
        if (mine) { if (!done) fprintf(f, "%s\n", line); done = 1; }
        else fprintf(f, "%.*s\n", ll, p);
        p = e ? e + 1 : 0;
    }
    if (!done) fprintf(f, "%s\n", line);
    free(text);
    int ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok &= fclose(f) == 0;
    if (!ok || rename(tmp, path)) { remove(tmp); return -1; }
    return 0;
}
static const BOpt *b_opt(int sys, int *n) {
    if (sys == SYS_NDS) { *n = ARRAY_LEN(B_NDS); return B_NDS; }
    *n = ARRAY_LEN(B_GBA); return B_GBA;
}
static void banana_load(int sys, int *choice) {
    int n; const BOpt *b = b_opt(sys, &n);
    for (int i = 0; i < n; i++) {
        choice[i] = b[i].o.rec;
        char v[128];
        /* the first file that has it (the DS's: SuperDrastic's, else BananaDS's) */
        for (int t = 0; t < 2 && b[i].t[t].file; t++) {
            if (!kv_get(b[i].t[t].file, b[i].t[t].key, v, sizeof v)) continue;
            for (int k = 0; k < b[i].o.n; k++) if (!strcmp(v, b[i].t[t].vals[0] ? b[i].t[t].vals[k] : b[i].o.values[k])) choice[i] = k;
            break;
        }
    }
}
static int banana_save(int sys, const int *choice, int only_missing) {
    int n, rc = 0, was[GOPT_MAX]; const BOpt *b = b_opt(sys, &n);
    banana_load(sys, was);
    for (int i = 0; i < n; i++) for (int t = 0; t < 2 && b[i].t[t].file; t++) {
        /* only what was changed here: a key BananaOS hasn't written yet keeps its own default */
        if (!only_missing && choice[i] == was[i]) continue;
        const BTarget *T = &b[i].t[t]; char v[128];
        if (only_missing && kv_get(T->file, T->key, v, sizeof v)) continue;
        int c = CLAMP(choice[i], 0, b[i].o.n - 1);
        if (kv_set(T->file, T->key, T->vals[0] ? T->vals[c] : b[i].o.values[c], T->style)) rc = -1;
    }
    return rc;
}

void gamecfg_load(int sys, int *choice) {
    if (cfg.banana) { banana_load(sys, choice); return; }
    const GameOpt *o; int n = gamecfg_opts(sys, &o);
    size_t len; char *text = slurp(cfg_path(), &len);
    for (int i = 0; i < n; i++) {
        choice[i] = o[i].rec;
        char key[200], val[128];
        snprintf(key, sizeof key, "%s.%s", plat(sys), o[i].key);
        if (!text || !find_key(text, key, val, sizeof val)) continue;
        for (int k = 0; k < o[i].n; k++) if (!strcmp(val, o[i].values[k])) choice[i] = k;
    }
    free(text);
}

/* writes the system's keys: all of them (only_missing 0), or those it hasn't got (1) */
static int save_keys(int sys, const int *choice, int only_missing) {
    if (cfg.banana) return banana_save(sys, choice, only_missing);
    const GameOpt *o; int n = gamecfg_opts(sys, &o);
    const char *path = cfg_path();
    /* ROCKNIX's own lock for this file (profile.d/001-functions), so nothing writes it at the same time */
    int lk = open("/tmp/.system.cfg.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    /* (a while at most: whatever holds it for good must not freeze the menu) */
    int locked = 0;
    for (int i = 0; lk >= 0 && i < 40 && !(locked = !flock(lk, LOCK_EX | LOCK_NB)); i++) usleep(25000);
    size_t len; char *text = slurp(path, &len);
    char tmp[600]; snprintf(tmp, sizeof tmp, "%s.dsixl", path);
    FILE *f = fopen(tmp, "w");
    int rc = -1;
    if (f) {
        int have[GOPT_MAX] = { 0 };
        for (const char *p = text; p && *p; ) {
            const char *e = strchr(p, '\n');
            int ll = (int)((e ? e : p + strlen(p)) - p), ours = -1;
            for (int i = 0; i < n; i++) {
                char key[200]; int kl = snprintf(key, sizeof key, "%s.%s=", plat(sys), o[i].key);
                if (ll >= kl && !strncmp(p, key, (size_t)kl)) ours = i;
            }
            if (ours >= 0) have[ours] = 1;
            if (ours < 0 || only_missing) fprintf(f, "%.*s\n", ll, p);
            p = e ? e + 1 : 0;
        }
        for (int i = 0; i < n; i++)
            if (!only_missing || !have[i]) fprintf(f, "%s.%s=%s\n", plat(sys), o[i].key, o[i].values[CLAMP(choice[i], 0, o[i].n - 1)]);
        if (fclose(f) == 0 && rename(tmp, path) == 0) rc = 0;
        else remove(tmp);
    }
    free(text);
    if (lk >= 0) { if (locked) flock(lk, LOCK_UN); close(lk); }
    return rc;
}
int gamecfg_save(int sys, const int *choice) { return save_keys(sys, choice, 0); }
int gamecfg_defaults(int sys) {
    const GameOpt *o; int n = gamecfg_opts(sys, &o), c[GOPT_MAX];
    for (int i = 0; i < n; i++) c[i] = o[i].rec;
    return save_keys(sys, c, 1);
}
