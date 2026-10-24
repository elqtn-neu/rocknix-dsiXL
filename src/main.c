// main.c: DSi Menu: a Nintendo DS and Game Boy Advance menu for the Anbernic RG DS Plus on BananaOS, the
// successor of the Nintendo DSi's, on both of its panels at their own 1024x768.
//
//   dsixl [--backend kms|sdl|headless] [--no-boot] [--return nds|gba <rom file name>]
//   dsixl --setup-emulators   the recommended emulator settings into ROCKNIX's system.cfg, where none are set
//   tests and pictures (headless): --script "<t>:<button>[~<held s>][,...]" --record <dir> --seconds <n>
//
// Exit status: 0 quit, 10 launch the game in /tmp/dsixl-launch, 20 EmulationStation, 21 power off, 22 reboot,
// 3 no display (see device/session.sh).
#define _GNU_SOURCE
#include "ui.h"
#include "config.h"
#include "library.h"
#include "audio.h"
#include "gamecfg.h"
#include "power.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>

static volatile sig_atomic_t term;
/* DSIXL_PROFILE: what each frame of the menu costs the CPU (its drawing, its present), logged every few seconds and,
 * in a headless run, summed up at the end */
static double cpu_now(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }
static int prof_on;
static double prof_ms[20000]; static int prof_n, prof_idle;
static void prof_add(double ms, int drew) { if (!drew) { prof_idle++; return; } if (prof_n < 20000) prof_ms[prof_n++] = ms; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void prof_report(const char *why) {
    if (!prof_n) { plat_log("profile %s: %d frames, none drawn", why, prof_idle); if (getenv("DSIXL_VERBOSE")) {} return; }
    qsort(prof_ms, (size_t)prof_n, sizeof(double), cmpd);
    double sum = 0; for (int i = 0; i < prof_n; i++) sum += prof_ms[i];
    char b[200];
    snprintf(b, sizeof b, "profile %s: %d drawn (%d idle): mean %.2f ms, p50 %.2f, p95 %.2f, max %.2f", why, prof_n, prof_idle,
             sum / prof_n, prof_ms[prof_n / 2], prof_ms[prof_n * 95 / 100], prof_ms[prof_n - 1]);
    plat_log("%s", b);
    if (prof_on > 1) fprintf(stderr, "%s\n", b);
    prof_n = prof_idle = 0;
}
/* the time, counting the time asleep too (the monotonic clock stops while the handheld sleeps) */
static double boottime(void) { struct timespec t; clock_gettime(CLOCK_BOOTTIME, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }
static void on_term(int s) { (void)s; term = 1; }

/* the scripted input of a headless run: "1.0:A,2.5:RIGHT,3:TOUCH@128x100" (each held 0.1 s); POWER (the power
 * button, pressed), LIDCLOSE and LIDOPEN */
enum { S_POWER = 100, S_LIDCLOSE, S_LIDOPEN };
typedef struct { double t, hold; int bit; int touch, tx, ty; } Ev;
static Ev evs[256]; static int nev;
static void parse_script(const char *s) {
    static const char *names[B_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "L2", "R2", "SELECT", "START", "MENU", "L3", "VOLUP", "VOLDN" };
    char buf[4096]; snprintf(buf, sizeof buf, "%s", s);
    for (char *tok = strtok(buf, ","); tok && nev < 256; tok = strtok(0, ",")) {
        char *c = strchr(tok, ':'); if (!c) continue;
        *c = 0; Ev e = { atof(tok), 0, -1, 0, 0, 0 };
        char *h = strchr(c + 1, '~'); if (h) { e.hold = atof(h + 1); *h = 0; }   /* "RIGHT~1.5": held 1.5 s */
        if (!strncasecmp(c + 1, "TOUCH@", 6)) { e.touch = 1; sscanf(c + 7, "%dx%d", &e.tx, &e.ty); }
        else if (!strcasecmp(c + 1, "POWER")) e.bit = S_POWER;
        else if (!strcasecmp(c + 1, "LIDCLOSE")) e.bit = S_LIDCLOSE;
        else if (!strcasecmp(c + 1, "LIDOPEN")) e.bit = S_LIDOPEN;
        else for (int b = 0; b < B_COUNT; b++) if (!strcasecmp(c + 1, names[b])) e.bit = b;
        evs[nev++] = e;
    }
}
static void script_input(double t, Input *in) {
    memset(in, 0, sizeof *in);
    for (int i = 0; i < nev; i++) {
        if (evs[i].bit == S_LIDCLOSE || evs[i].bit == S_LIDOPEN) { if (t >= evs[i].t) in->lid = evs[i].bit == S_LIDCLOSE; continue; }
        if (evs[i].bit == S_POWER) { if (t >= evs[i].t && t < evs[i].t + 1.0 / 60) in->power = 1; continue; }
        double held = evs[i].hold > 0 ? evs[i].hold : evs[i].touch ? 0.12 : 0.1;
        if (t >= evs[i].t && t < evs[i].t + held) {
            if (evs[i].touch) { in->touch[1] = 1; in->tx[1] = evs[i].tx; in->ty[1] = evs[i].ty; }
            else if (evs[i].bit >= 0) in->held |= BIT(evs[i].bit);
        }
    }
}

int main(int argc, char **argv) {
    const char *backend = 0, *script = 0, *record = 0;
    double seconds = 0;
    UiStart st; memset(&st, 0, sizeof st);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--backend") && i + 1 < argc) backend = argv[++i];
        else if (!strcmp(argv[i], "--no-boot")) st.no_boot = 1;
        else if (!strcmp(argv[i], "--return") && i + 2 < argc) {
            st.ret_sys = !strcasecmp(argv[i + 1], "gba") ? SYS_GBA : SYS_NDS;
            snprintf(st.ret_file, sizeof st.ret_file, "%s", argv[i + 2]); i += 2;
        }
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--setup-emulators")) {
            /* the installer: the recommended emulator settings, where the system has none of its own yet */
            int rc = 0;
            for (int s = 0; s < SYS_COUNT; s++) rc |= gamecfg_defaults(s);
            printf(rc ? "Couldn't write ROCKNIX's system.cfg\n" : "Emulator settings: the recommended ones set where none were\n");
            return rc ? 1 : 0;
        }
        else fprintf(stderr, "dsixl: ignoring %s (usage: %s [--backend kms|sdl|headless] [--no-boot] [--return nds|gba <file>])\n", argv[i], argv[0]);
    }
    signal(SIGTERM, on_term); signal(SIGINT, on_term);
    prof_on = getenv("DSIXL_PROFILE") ? atoi(getenv("DSIXL_PROFILE")) + 0 : 0; if (getenv("DSIXL_PROFILE") && !prof_on) prof_on = 1;
    config_load();
    library_scan();
    plat_log("dsixl: %d NDS, %d GBA games; home %s", lib[SYS_NDS].n, lib[SYS_GBA].n, cfg.home);
    PlatInfo info;
    plat_native = 1;
    if (cfg.banana) setenv("DSIXL_NO_GRAB", "1", 1);   /* (BananaOS's bananad has the power button and the lid) */
    if (plat_init(backend, &info)) { plat_log("no display"); fprintf(stderr, "dsixl: no display\n"); return 3; }
    plat_log("display: %s, %dx scale", info.backend, info.scale);
    plat_set_lcd(cfg.lcd);
    int headless = !strcmp(info.backend, "headless");
    if (script) parse_script(script);
    audio_init();
    ui_init(&st);
    Surf top, bot;
    surf_alloc(&top, info.top_w, info.top_h); surf_alloc(&bot, info.bot_w, info.bot_h);
    if (info.top_w != 1024 || info.top_h != 768) plat_log("display: %dx%d (the menu is drawn for 1024x768)", info.top_w, info.top_h);
    double last = plat_now(), vt = 0, last_bt = boottime();
    int frame = 0, code = -1, applied = PW_FULL;
    while (code < 0) {
        Input in;
        double dt;
        if (headless) {
            dt = 1.0 / 60; vt += dt;
            script_input(vt, &in);
            if (seconds > 0 && vt >= seconds) break;
        } else {
            plat_poll(&in);
            double now = plat_now(), bt = boottime(); dt = now - last; last = now;
            /* seconds gone that the menu didn't see: the handheld slept (bananad stops the menu and suspends) */
            if ((bt - last_bt > 2.5 || dt > 3) && applied != PW_OFF) ui_resumed();
            last_bt = bt;
            if (dt > 0.1) dt = 0.1;
            /* power: the backlights follow the menu's wish; with the screens off nothing is drawn, a button wakes them */
            int pw = ui_power();
            if (pw != applied) {
                power_screens(pw != PW_OFF);
                power_dim(pw >= PW_DIM);
                applied = pw;
            }
            if (pw == PW_OFF) {
                if (term) break;
                ui_idle_input(&in);
                usleep(50000);
                last = plat_now(); last_bt = boottime();
                continue;
            }
        }
        if (term) in.quit = 1;
        double c0 = prof_on ? cpu_now() : 0;
        code = ui_frame(&in, dt, &top, &bot);
        double c1 = prof_on ? cpu_now() : 0;
        if (headless && record) {
            /* DSIXL_SNAP="1.5,3": only the frames at those times (seconds), named by them; else every frame */
            const char *at = getenv("DSIXL_SNAP");
            char p[600];
            if (!at) { snprintf(p, sizeof p, "%s/%05d.png", record, frame); headless_snapshot(p); }
            else for (const char *q = at; q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : 0) {
                double ts = atof(q);
                if (ts <= vt && ts > vt - dt) { snprintf(p, sizeof p, "%s/t%06.2f.png", record, ts); headless_snapshot(p); }
            }
        }
        plat_present(&top, &bot);
        if (prof_on) {
            double ms = (cpu_now() - c0) * 1000;
            prof_add(ms, ui_drew());
            if (prof_on > 2 && ms > 6) fprintf(stderr, "slow frame at %.2f s: %.1f ms\n", headless ? vt : plat_now(), ms);
            (void)c1;
            static double next; if (!headless && plat_now() > next) { if (next) prof_report("5 s"); next = plat_now() + 5; }
        }
        if (!headless && ui_power() >= PW_CALM) usleep(15000);     /* calm: every other refresh is plenty */
        if (!headless && ui_asleep()) usleep(40000);               /* asleep: only waiting to wake */
        frame++;
    }
    if (prof_on) prof_report("run");
    if (!headless) power_restore();
    plat_shutdown();
    plat_log("dsixl: exit %d", code < 0 ? 0 : code);
    return code < 0 ? 0 : code;
}
