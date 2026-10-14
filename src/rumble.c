// rumble.c: the RG DS Plus's vibration motor. ROCKNIX's device tree gives it to the kernel's pwm-vibrator driver,
// which shows it as an input device ("pwm-vibrator") taking force-feedback rumble effects: an effect is uploaded
// once and played with a length each time. Without the device (a desktop) every call does nothing.
#define _GNU_SOURCE
#include "rumble.h"
#include "config.h"
#include "plat/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <glob.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static int fd = -1, effect_id = -1, tried;
static double pending_at = -1; static int pending_ms; static float pending_k;

static int has_rumble(int f) {
    unsigned long ff[(FF_MAX + 1) / (8 * sizeof(long)) + 1] = { 0 };
    if (ioctl(f, EVIOCGBIT(EV_FF, sizeof ff), ff) < 0) return 0;
    return (ff[FF_RUMBLE / (8 * sizeof(long))] >> (FF_RUMBLE % (8 * sizeof(long)))) & 1;
}

static void open_motor(void) {
    tried = 1;
    glob_t g;
    if (glob("/dev/input/event*", 0, 0, &g)) return;
    for (size_t i = 0; i < g.gl_pathc && fd < 0; i++) {
        int f = open(g.gl_pathv[i], O_RDWR | O_CLOEXEC);
        if (f < 0) continue;
        char name[128] = ""; ioctl(f, EVIOCGNAME(sizeof name - 1), name);
        /* the motor itself first; a pad with rumble would do too */
        if (has_rumble(f) && (strstr(name, "vibra") || strstr(name, "rumble") || strstr(name, "haptic"))) { fd = f; plat_log("rumble: %s (%s)", g.gl_pathv[i], name); }
        else close(f);
    }
    globfree(&g);
    if (fd < 0) plat_log("rumble: no motor");
}

static void play(int ms, float strength) {
    if (!cfg.rumble || ms <= 0) return;
    if (!tried) open_motor();
    if (fd < 0) return;
    struct ff_effect e; memset(&e, 0, sizeof e);
    e.type = FF_RUMBLE; e.id = (int16_t)effect_id;
    e.u.rumble.strong_magnitude = (uint16_t)(clampf(strength, 0, 1) * 0xffff);
    e.u.rumble.weak_magnitude = e.u.rumble.strong_magnitude;
    e.replay.length = (uint16_t)ms;
    if (ioctl(fd, EVIOCSFF, &e) < 0) { plat_log("rumble: upload failed"); close(fd); fd = -1; return; }
    effect_id = e.id;
    struct input_event ev; memset(&ev, 0, sizeof ev);
    ev.type = EV_FF; ev.code = (uint16_t)effect_id; ev.value = 1;
    if (write(fd, &ev, sizeof ev) < 0) { /* the motor went away: try again later */ close(fd); fd = -1; tried = 0; }
}

void rumble(int ms, float strength) { play(ms, strength); }
void rumble_double(int ms, float strength) { play(ms, strength); pending_at = plat_now() + (ms + 55) / 1000.0; pending_ms = ms; pending_k = strength; }
void rumble_tick(void) {
    if (pending_at >= 0 && plat_now() >= pending_at) { pending_at = -1; play(pending_ms, pending_k); }
}
