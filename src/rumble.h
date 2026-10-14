// rumble.h: short buzzes of the vibration motor (Options > Vibration, config.ini rumble).
#pragma once

void rumble(int ms, float strength);            /* strength 0..1 */
void rumble_double(int ms, float strength);     /* two buzzes in a row (the end of the list) */
void rumble_tick(void);                          /* every frame: plays the second of a double */
