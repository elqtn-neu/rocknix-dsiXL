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
