// panels.h: the menu's windows that ask something: a list to pick from (Wi-Fi networks, time zones, ...), a
// DSi-style touch keyboard (a Wi-Fi password), a "working on it" window. Each takes this frame's input and draws
// itself; the menu (ui.c) decides what to do with the answer.
#pragma once
#include "gfx.h"
#include "common.h"

typedef struct {
    double t;                           /* the menu's clock */
    uint32_t pressed, repeat;           /* B_* bits: went down this frame; down or repeating (held directions) */
    int a, b;                           /* the confirm and back buttons (B_*: swap_ab) */
    int down, began, ended, moved;      /* the bottom screen's touch */
    int x, y, x0, y0;
} PanelIn;

/* ---- a list to pick from ---- */
typedef struct {
    char label[72], note[40];           /* note: small, on the right */
    int sig;                            /* Wi-Fi: signal 0..4 (-1: none) */
    int lock, check;                    /* a padlock (a password is needed), a tick (the one in use) */
} PickItem;

typedef struct {
    char title[64], hint[96];           /* hint: the top screen's line about the list */
    PickItem it[640]; int n;
    int sel; Spring scroll; float drag0, want;
    int open; float anim;
    int touch_i;
} Picker;

void picker_open(Picker *p, const char *title, const char *hint);
PickItem *picker_add(Picker *p, const char *label);
/* -1 still choosing, -2 cancelled, else the chosen item */
int picker_update(Picker *p, const PanelIn *in, float dt);
void picker_draw(Picker *p, Surf *bot, Surf *top);

/* ---- the keyboard ---- */
typedef struct {
    char title[64], prompt[96];
    char text[72];
    int open; float anim;
    int shift, sym;                     /* shift: 1 once, 2 locked */
    int kx, ky;                         /* the key the d-pad is on */
    int touch_key; double key_t, now; int key_last;  /* the key under the finger, the last pressed (its pop) */
} Kbd;

void kbd_open(Kbd *k, const char *title, const char *prompt, const char *text);
/* -1 typing, 0 cancelled, 1 OK (the text in k->text) */
int kbd_update(Kbd *k, const PanelIn *in, float dt);
void kbd_draw(Kbd *k, Surf *bot, Surf *top);

/* ---- working on it: a small window with turning dots ---- */
void busy_draw(Surf *s, const char *line1, const char *line2, double t, float anim, float progress);  /* progress <0: none */
