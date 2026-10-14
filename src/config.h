// config.h: the settings (config.ini), the remembered state (state.ini) and what the handheld reports (battery,
// clock). Everything lives under one folder: DSIXL_HOME, else /storage/.config/dsixl, else next to the binary.
#pragma once
#include "common.h"

typedef struct {
    char user[32];
    int color;                      /* 0..15: the DS's favourite colours (Settings > User > Color) */
    int clock24, sfx, music, rumble;
    int boot_splash;                /* 1: the health and safety screen at start; 0: straight to the menu */
    int boot_wait;                  /* 0: it waits for a touch or a button, as the DSi; n: continues after n s */
    int swap_ab;                    /* Nintendo layout (A right) is the default */
    int bday_month, bday_day;       /* the user's birthday (0: not set), as the DS keeps it */
    char dir_nds[256], dir_gba[256], media[256], sounds[256];
    char home[256], cache[300];
    /* state */
    int sys;                        /* 0 NDS, 1 GBA */
    char last[2][256];              /* the selected game per system (file name) */
} Config;

extern Config cfg;

void config_load(void);
int file_exists_cfg(const char *path);
void config_save(void);           /* the settings */
void state_save(void);            /* the selection */

/* the DS's 16 favourite colours: the accent of the menu (the selection frame, the user name, the scroll bar) */
uint32_t fav_color(int i);
const char *fav_color_name(int i);

/* the handheld */
int battery_level(int *charging);   /* 0..100, -1 without a battery */
int is_birthday(void);
int is_snow_season(void);           /* December */
void clock_text(char *date, int dsz, char *tm, int tsz, int *colon_on);
