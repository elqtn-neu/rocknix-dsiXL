// main.c: DSi Menu for ROCKNIXDS: a Nintendo DS and Game Boy Advance menu for the Anbernic RG DS Plus in the
// style of the Nintendo DSi's, on both panels at the DS's own resolution.
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

static volatile sig_atomic_t term;
static void on_term(int s) { (void)s; term = 1; }

/* the scripted input of a headless run: "1.0:A,2.5:RIGHT,3:TOUCH@128x100" (each held 0.1 s) */
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
        else for (int b = 0; b < B_COUNT; b++) if (!strcasecmp(c + 1, names[b])) e.bit = b;
        evs[nev++] = e;
    }
}
static void script_input(double t, Input *in) {
    memset(in, 0, sizeof *in);
    for (int i = 0; i < nev; i++) {
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
    config_load();
    library_scan();
    plat_log("dsixl: %d NDS, %d GBA games; home %s", lib[SYS_NDS].n, lib[SYS_GBA].n, cfg.home);
    PlatInfo info;
    if (plat_init(backend, &info)) { plat_log("no display"); fprintf(stderr, "dsixl: no display\n"); return 3; }
    plat_log("display: %s, %dx scale", info.backend, info.scale);
    plat_set_lcd(cfg.lcd);
    int headless = !strcmp(info.backend, "headless");
    if (script) parse_script(script);
    audio_init();
    ui_init(&st);
    Surf top, bot;
    surf_alloc(&top, 256, 192); surf_alloc(&bot, 256, 192);
    double last = plat_now(), vt = 0;
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
            double now = plat_now(); dt = now - last; last = now;
            /* frozen for seconds with the screens on: the handheld slept (ROCKNIX's lid suspend stops the menu) */
            if (dt > 3 && applied != PW_OFF) ui_resumed();
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
                last = plat_now();
                continue;
            }
        }
        if (term) in.quit = 1;
        code = ui_frame(&in, dt, &top, &bot);
        if (headless && record) { char p[600]; snprintf(p, sizeof p, "%s/%05d.png", record, frame); headless_snapshot(p); }
        plat_present(&top, &bot);
        if (!headless && ui_power() >= PW_CALM) usleep(15000);     /* calm: every other refresh is plenty */
        frame++;
    }
    if (!headless) power_restore();
    plat_shutdown();
    plat_log("dsixl: exit %d", code < 0 ? 0 : code);
    return code < 0 ? 0 : code;
}
