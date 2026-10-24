// sleepkey.h: the menu's Sleep on BananaOS: a press of a power button of its own, which bananad takes as yours.
#pragma once
void sleepkey_init(void);      /* at start (bananad needs a moment to see a new device) */
int sleepkey_tap(void);        /* press and release; -1 when there's no uinput */
void sleepkey_close(void);
