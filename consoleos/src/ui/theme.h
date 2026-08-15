/* ConsoleOS - système de thème
 *
 * Toutes les couleurs, polices et fonds d'écran sont chargés depuis un
 * fichier theme.json (voir docs/theme-format.md) : rien n'est codé en dur
 * dans le programme, ce qui permet à l'utilisateur de personnaliser
 * l'interface (thème, couleurs, fond d'écran, sons) sans recompiler.
 */
#ifndef CONSOLEOS_THEME_H
#define CONSOLEOS_THEME_H

#include <SDL2/SDL.h>

typedef struct {
    SDL_Color bg;
    SDL_Color panel;
    SDL_Color accent;
    SDL_Color text;
    SDL_Color text_dim;
    SDL_Color highlight;
    char font_path[512];
    char wallpaper_path[512];
    char sound_nav[512];
    char sound_select[512];
    int corner_radius;
} theme_t;

/* Charge un thème depuis un dossier contenant theme.json + ses ressources.
 * Retourne 0 en cas de succès, remplit *out avec des valeurs par défaut
 * sûres en cas d'échec de lecture (l'interface ne doit jamais planter
 * faute de thème). */
int theme_load(const char *theme_dir, theme_t *out);

#endif /* CONSOLEOS_THEME_H */
