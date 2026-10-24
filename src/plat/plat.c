// plat.c: picks a backend and holds what all of them share (time, the data directory, the log).
#define _GNU_SOURCE
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

static const Backend *be;
static FILE *logf_;

void plat_log(const char *fmt, ...) {
    va_list ap;
    if (!logf_) {
        /* DSIXL_LOG_NAME: another program on this platform layer (the ROCKNIXDS Store) names its own log */
        const char *name = getenv("DSIXL_LOG_NAME");
        char p[512], old[520]; snprintf(p, sizeof p, "%s/%s", plat_data_dir(), name && *name ? name : "dsixl.log");
        snprintf(old, sizeof old, "%s.1", p); rename(p, old);   /* the last session's log is kept beside it */
        logf_ = fopen(p, "w");
        if (logf_) setvbuf(logf_, 0, _IOLBF, 0);
    }
    va_start(ap, fmt);
    if (logf_) { vfprintf(logf_, fmt, ap); fputc('\n', logf_); }
    va_end(ap);
    if (getenv("DSIXL_VERBOSE")) { va_start(ap, fmt); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap); }
}

static void mkdirs(const char *path) {
    char tmp[512]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

/* DSIXL_DATA, else ROCKNIX's config partition, else XDG_DATA_HOME or ~/.local/share */
const char *plat_data_dir(void) {
    static char dir[512];
    if (dir[0]) return dir;
    const char *e = getenv("DSIXL_DATA");
    struct stat st;
    if (e && *e) snprintf(dir, sizeof dir, "%s", e);
    else if (stat("/storage/.config", &st) == 0) snprintf(dir, sizeof dir, "/storage/.config/dsixl/data");
    else if ((e = getenv("XDG_DATA_HOME")) && *e) snprintf(dir, sizeof dir, "%s/dsixl", e);
    else if ((e = getenv("HOME")) && *e) snprintf(dir, sizeof dir, "%s/.local/share/dsixl", e);
    else snprintf(dir, sizeof dir, "/tmp/dsixl");
    mkdirs(dir);
    return dir;
}

double plat_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

int plat_init(const char *name, PlatInfo *info) {
    memset(info, 0, sizeof *info);
    const Backend *order[3]; int n = 0;
    if (name && !strcmp(name, "kms")) order[n++] = &backend_kms;
    else if (name && !strcmp(name, "sdl")) order[n++] = &backend_sdl;
    else if (name && !strcmp(name, "headless")) order[n++] = &backend_headless;
    else {   /* auto: the panels if we may have them, else a window */
        order[n++] = &backend_kms; order[n++] = &backend_sdl;
    }
    for (int i = 0; i < n; i++) {
        if (order[i]->init(info) == 0) { be = order[i]; return 0; }
    }
    return -1;
}
void plat_poll(Input *in) { be->poll(in); }
void plat_present(Surf *top, Surf *bot) { be->present(top, bot); }
const char *plat_pace(void) { return be && be->pace ? be->pace() : ""; }
double plat_frame_clock(void) { return be && be->frame_clock ? be->frame_clock() : 0; }
void plat_shutdown(void) { if (be && be->shutdown) be->shutdown(); }
int plat_audio_start(int rate, void (*mix)(int16_t *, int)) { return be && be->audio ? be->audio(rate, mix) : -1; }
void plat_set_title(const char *s) { (void)s; }
int plat_native;
int plat_dmg_on, plat_dmg[2][2] = { { 1 << 30, 0 }, { 1 << 30, 0 } };
void plat_damage(int screen, int y0, int y1) {
    if (screen < 0 || screen > 1 || y1 <= y0) { plat_dmg_on = 1; return; }
    plat_dmg_on = 1;
    if (y0 < plat_dmg[screen][0]) plat_dmg[screen][0] = y0;
    if (y1 > plat_dmg[screen][1]) plat_dmg[screen][1] = y1;
}
void plat_grab_sys(int on) { if (be == &backend_kms) evdev_grab_sys(on); }
