// power.h: the menu's own power saving (the screens dimmed, then off, after a while untouched) and what the battery
// and the games card report for the options window.
#pragma once
#include "common.h"

void power_dim(int on);                 /* both backlights to 30% of where they are (and back) */
void power_screens(int on);             /* both backlights off (and on) */
void power_restore(void);               /* everything back: call before leaving */
int power_lit(void);                    /* the backlight is on (1), off (0: ROCKNIX's sleep has it), unknown (-1) */
/* what the handheld is drawing now (watts) and the hours the battery has left at that rate; -1 where unknown */
int power_draw(float *watts, float *hours_left);
int disk_free_gb(const char *path, float *free_gb, float *total_gb);
