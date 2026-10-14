// img.c: pictures from disk (PNG, JPEG, BMP, GIF through stb_image), and a small cache of them already shrunk to
// the size the menu draws them at, so a box art scan of 1-2 MB costs a few KB once it has been seen.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO_FAIL_MESSAGES
#include "third_party/stb_image.h"
#include "img.h"
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>

int img_load(const char *path, Img *out) {
    memset(out, 0, sizeof *out);
    int w, h, n;
    uint8_t *d = stbi_load(path, &w, &h, &n, 4);
    if (!d) return -1;
    if (img_alloc(out, w, h)) { stbi_image_free(d); return -1; }
    for (int i = 0; i < w * h; i++) out->px[i] = ARGB(d[i * 4 + 3], d[i * 4], d[i * 4 + 1], d[i * 4 + 2]);
    stbi_image_free(d);
    return 0;
}

int file_exists(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }
long long file_mtime(const char *p) { struct stat st; return stat(p, &st) ? 0 : (long long)st.st_mtime * 1000 + (long long)(st.st_size & 999); }

/* the cache: raw ARGB with a header, keyed by the source path, its mtime and the size wanted */
typedef struct { char magic[4]; int32_t w, h; int64_t key; } CacheHead;

static void cache_name(const char *dir, const char *path, int w, int h, int mode, char *out, size_t sz) {
    snprintf(out, sz, "%s/%08x-%dx%d-%d.raw", dir, hash_str(path), w, h, mode);
}

int img_load_fit(const char *path, int max_w, int max_h, int mode, const char *cache_dir, Img *out) {
    memset(out, 0, sizeof *out);
    if (!path || !*path) return -1;
    long long key = file_mtime(path);
    if (!key) return -1;
    char cpath[600] = "";
    if (cache_dir) {
        cache_name(cache_dir, path, max_w, max_h, mode, cpath, sizeof cpath);
        FILE *f = fopen(cpath, "rb");
        if (f) {
            CacheHead hd;
            if (fread(&hd, sizeof hd, 1, f) == 1 && !memcmp(hd.magic, "DSXC", 4) && hd.key == key && hd.w > 0 && hd.h > 0 &&
                hd.w <= 4096 && hd.h <= 4096 && !img_alloc(out, hd.w, hd.h) &&
                fread(out->px, 4, (size_t)hd.w * hd.h, f) == (size_t)hd.w * hd.h) { fclose(f); return 0; }
            img_free(out);
            fclose(f);
        }
    }
    Img full;
    if (img_load(path, &full)) return -1;
    if (mode == FIT_COVER) img_resize_cover(&full, out, max_w, max_h, 0.4f);
    else {
        float k = MIN((float)max_w / full.w, (float)max_h / full.h);
        if (mode == FIT_CONTAIN_DOWN) k = MIN(k, 1.0f);
        int w = MAX(1, (int)(full.w * k + 0.5f)), h = MAX(1, (int)(full.h * k + 0.5f));
        if (w == full.w && h == full.h) { *out = full; full.px = 0; }
        else img_resize_area(&full, out, w, h);
    }
    img_free(&full);
    if (!out->px) return -1;
    if (cpath[0]) {
        /* (its own temporary name: the cache-warming thread may be making the same one) */
        char tmp[640]; snprintf(tmp, sizeof tmp, "%s.%lx.tmp", cpath, (unsigned long)pthread_self());
        FILE *f = fopen(tmp, "wb");
        if (f) {
            CacheHead hd = { { 'D', 'S', 'X', 'C' }, out->w, out->h, key };
            int ok = fwrite(&hd, sizeof hd, 1, f) == 1 && fwrite(out->px, 4, (size_t)out->w * out->h, f) == (size_t)out->w * out->h;
            if (fclose(f) == 0 && ok) rename(tmp, cpath); else remove(tmp);
        }
    }
    return 0;
}

/* the opaque part of an image: logos often come with wide transparent margins */
void img_trim(Img *im) {
    int x0 = im->w, y0 = im->h, x1 = -1, y1 = -1;
    for (int y = 0; y < im->h; y++) for (int x = 0; x < im->w; x++)
        if (CA(im->px[(size_t)y * im->w + x]) > 8) { x0 = MIN(x0, x); x1 = MAX(x1, x); y0 = MIN(y0, y); y1 = MAX(y1, y); }
    if (x1 < 0 || (x0 == 0 && y0 == 0 && x1 == im->w - 1 && y1 == im->h - 1)) return;
    Img t;
    if (img_alloc(&t, x1 - x0 + 1, y1 - y0 + 1)) return;
    for (int y = 0; y < t.h; y++) memcpy(t.px + (size_t)y * t.w, im->px + (size_t)(y0 + y) * im->w + x0, (size_t)t.w * 4);
    img_free(im);
    *im = t;
}
