// gamecfg.c: see gamecfg.h. The recommended choices are the balance of looks, speed and battery that ROCKNIXDS
// measured on the RG DS Plus (its dsflip/device/session.sh), and the plain good defaults for the GBA.
#define _GNU_SOURCE
#include "gamecfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

static const GameOpt NDS_OPTS[] = {
    { "Screen", "shader", "How the DS's pixels are drawn on the big screens.", 5, 0,
      { "Crisp pixels", "Pixel grid", "Crisp, DS colors", "Smooth", "Pixel-perfect" },
      { "ds-crisp", "ds-grid", "ds-crisp-color", "none", "ds-integer" },
      { "Sharp, exactly 4x on this screen. Barely uses the GPU.", "Sharp, with the DS's faint pixel grid.",
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

int gamecfg_opts(int sys, const GameOpt **opts) {
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

void gamecfg_load(int sys, int *choice) {
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
    const GameOpt *o; int n = gamecfg_opts(sys, &o);
    const char *path = cfg_path();
    /* ROCKNIX's own lock for this file (profile.d/001-functions), so nothing writes it at the same time */
    int lk = open("/tmp/.system.cfg.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (lk >= 0) flock(lk, LOCK_EX);
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
    if (lk >= 0) { flock(lk, LOCK_UN); close(lk); }
    return rc;
}
int gamecfg_save(int sys, const int *choice) { return save_keys(sys, choice, 0); }
int gamecfg_defaults(int sys) {
    const GameOpt *o; int n = gamecfg_opts(sys, &o), c[GOPT_MAX];
    for (int i = 0; i < n; i++) c[i] = o[i].rec;
    return save_keys(sys, c, 1);
}
