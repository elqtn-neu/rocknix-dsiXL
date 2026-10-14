// power.c: see power.h.
#define _GNU_SOURCE
#include "power.h"
#include <stdio.h>
#include <stdlib.h>
#include <glob.h>
#include <sys/statvfs.h>

#define MAX_BL 4
static char bl_dir[MAX_BL][200]; static int bl_n = -1, bl_saved[MAX_BL], dimmed, off;

static void find_backlights(void) {
    if (bl_n >= 0) return;
    bl_n = 0;
    glob_t g;
    if (glob("/sys/class/backlight/*", 0, 0, &g)) return;
    for (size_t i = 0; i < g.gl_pathc && bl_n < MAX_BL; i++) snprintf(bl_dir[bl_n++], sizeof bl_dir[0], "%s", g.gl_pathv[i]);
    globfree(&g);
}
static int rd(const char *dir, const char *name) {
    char p[260]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "r"); int v = -1;
    if (f) { if (fscanf(f, "%d", &v) != 1) v = -1; fclose(f); }
    return v;
}
static void wr(const char *dir, const char *name, int v) {
    char p[260]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%d\n", v); fclose(f); }
}

void power_dim(int on) {
    find_backlights();
    if (on == dimmed) return;
    for (int i = 0; i < bl_n; i++) {
        if (on) { bl_saved[i] = rd(bl_dir[i], "brightness"); if (bl_saved[i] > 0) wr(bl_dir[i], "brightness", MAX(1, bl_saved[i] * 3 / 10)); }
        else if (bl_saved[i] > 0) wr(bl_dir[i], "brightness", bl_saved[i]);
    }
    dimmed = on;
}

void power_screens(int on) {
    find_backlights();
    if ((!on) == off) return;          /* already that way */
    for (int i = 0; i < bl_n; i++) wr(bl_dir[i], "bl_power", on ? 0 : 4);
    off = !on;
}

void power_restore(void) { power_screens(1); power_dim(0); }

/* the battery, as the kernel's fuel gauge has it (rk817: µA, µV, µAh) */
static int ps_read(const char *name, long *v) {
    glob_t g; int ok = 0;
    if (glob("/sys/class/power_supply/*/type", 0, 0, &g)) return 0;
    for (size_t i = 0; i < g.gl_pathc && !ok; i++) {
        FILE *f = fopen(g.gl_pathv[i], "r"); char t[32] = "";
        if (!f) continue;
        if (!fgets(t, sizeof t, f)) t[0] = 0;
        fclose(f);
        if (strncmp(t, "Battery", 7)) continue;
        char p[300]; snprintf(p, sizeof p, "%s", g.gl_pathv[i]); *strrchr(p, '/') = 0;
        char q[340]; snprintf(q, sizeof q, "%s/%s", p, name);
        if ((f = fopen(q, "r"))) { ok = fscanf(f, "%ld", v) == 1; fclose(f); }
    }
    globfree(&g);
    return ok;
}

int power_draw(float *watts, float *hours_left) {
    long cur, volt, chg;
    *watts = -1; *hours_left = -1;
    if (getenv("DSIXL_FAKE_DRAW")) { *watts = (float)atof(getenv("DSIXL_FAKE_DRAW")); *hours_left = 4000 * 3.8f / 1000 / *watts; return 0; }
    if (!ps_read("current_now", &cur) || cur == 0) return -1;
    if (ps_read("voltage_now", &volt) && volt > 0) *watts = (float)labs(cur) / 1e6f * (float)volt / 1e6f;
    if (ps_read("charge_now", &chg) && chg > 0) *hours_left = (float)chg / (float)labs(cur);
    return 0;
}

int disk_free_gb(const char *path, float *free_gb, float *total_gb) {
    struct statvfs v;
    if (statvfs(path, &v)) return -1;
    *free_gb = (float)((double)v.f_bavail * v.f_frsize / 1e9);
    *total_gb = (float)((double)v.f_blocks * v.f_frsize / 1e9);
    return 0;
}
