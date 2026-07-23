#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void default_theme(theme_t *t) {
    memset(t, 0, sizeof(*t));
    t->bg        = (SDL_Color){ 18, 18, 24, 255 };
    t->panel     = (SDL_Color){ 30, 30, 40, 235 };
    t->accent    = (SDL_Color){ 88, 166, 255, 255 };
    t->text      = (SDL_Color){ 240, 240, 245, 255 };
    t->text_dim  = (SDL_Color){ 150, 150, 160, 255 };
    t->highlight = (SDL_Color){ 255, 180, 60, 255 };
    t->corner_radius = 12;
}

static int read_file(const char *path, char *buf, size_t buflen) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, buflen - 1, f);
    fclose(f);
    buf[n] = 0;
    return 0;
}

static int json_color(const char *json, const char *key, SDL_Color *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p = strchr(p + strlen(pattern), '[');
    if (!p) return -1;
    int r, g, b, a = 255;
    int n = sscanf(p, "[%d,%d,%d,%d]", &r, &g, &b, &a);
    if (n < 3) return -1;
    out->r = (Uint8)r; out->g = (Uint8)g; out->b = (Uint8)b; out->a = (Uint8)a;
    return 0;
}

static int json_string(const char *json, const char *key, char *out, size_t outlen) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return -1;
    p = strchr(p, '"');
    if (!p) return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= outlen) len = outlen - 1;
    memcpy(out, p, len);
    out[len] = 0;
    return 0;
}

int theme_load(const char *theme_dir, theme_t *out) {
    default_theme(out);

    char path[600];
    snprintf(path, sizeof(path), "%s/theme.json", theme_dir);

    char buf[8192];
    if (read_file(path, buf, sizeof(buf)) != 0) {
        fprintf(stderr, "theme: '%s' introuvable, thème par défaut utilisé\n", path);
        return -1;
    }

    json_color(buf, "bg", &out->bg);
    json_color(buf, "panel", &out->panel);
    json_color(buf, "accent", &out->accent);
    json_color(buf, "text", &out->text);
    json_color(buf, "text_dim", &out->text_dim);
    json_color(buf, "highlight", &out->highlight);

    char rel[400];
    if (json_string(buf, "font", rel, sizeof(rel)) == 0)
        snprintf(out->font_path, sizeof(out->font_path), "%s/%s", theme_dir, rel);
    if (json_string(buf, "wallpaper", rel, sizeof(rel)) == 0)
        snprintf(out->wallpaper_path, sizeof(out->wallpaper_path), "%s/%s", theme_dir, rel);
    if (json_string(buf, "sound_nav", rel, sizeof(rel)) == 0)
        snprintf(out->sound_nav, sizeof(out->sound_nav), "%s/%s", theme_dir, rel);
    if (json_string(buf, "sound_select", rel, sizeof(rel)) == 0)
        snprintf(out->sound_select, sizeof(out->sound_select), "%s/%s", theme_dir, rel);

    return 0;
}
