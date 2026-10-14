// ui.h: the menu: the boot screens, the two screens of the menu, the animations, the options.
#pragma once
#include "plat/plat.h"

enum { EXIT_QUIT = 0, EXIT_LAUNCH = 10, EXIT_TO_ES = 20, EXIT_POWEROFF = 21, EXIT_REBOOT = 22 };

typedef struct {
    int no_boot;                    /* straight to the menu */
    int ret_sys; char ret_file[256];/* back from a game: that cartridge comes out of its slot */
} UiStart;

void ui_init(const UiStart *st);
/* one frame: input in, time step dt; returns -1 to go on, else the exit code */
int ui_frame(const Input *in, double dt, Surf *top, Surf *bot);

/* power: how much the menu needs the screens now. 0 all of it; 1 calm (nothing touched for a while: 30 frames a
 * second do); 2 dim the screens; 3 screens off (no frames at all: call ui_idle_input instead of ui_frame) */
enum { PW_FULL, PW_CALM, PW_DIM, PW_OFF };
int ui_power(void);
void ui_idle_input(const Input *in);    /* with the screens off: a button or a touch wakes them (and does nothing else) */
void ui_resumed(void);                  /* the handheld was asleep (the lid): the menu wakes up */
