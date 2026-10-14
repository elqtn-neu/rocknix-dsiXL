// img.h: pictures from disk, and a cache of them shrunk to the size they're drawn at.
#pragma once
#include "gfx.h"

enum { FIT_CONTAIN, FIT_CONTAIN_DOWN, FIT_COVER };   /* within max_w x max_h; only ever smaller; fill and crop */

int img_load(const char *path, Img *out);
/* loads and resizes, through cache_dir (NULL: no cache) */
int img_load_fit(const char *path, int max_w, int max_h, int mode, const char *cache_dir, Img *out);
void img_trim(Img *im);
int file_exists(const char *path);
long long file_mtime(const char *path);
