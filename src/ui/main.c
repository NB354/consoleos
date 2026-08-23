/* ConsoleOS - consoleos-ui
 *
 * Compositeur et interface graphique de la console. Développée
 * spécifiquement pour ConsoleOS (aucun bureau Linux), entièrement
 * pilotable à la manette et au clavier (flèches, Entrée, Échap
 * uniquement dans l'interface — jamais dans les jeux).
 *
 * Vues :
 *   HOME       - grille de la bibliothèque de jeux (icônes + jaquettes)
 *   GAME_INFO  - détails d'un jeu sélectionné (temps de jeu, version...)
 *   SETTINGS   - thème, couleurs, fond d'écran, sons, disposition
 *   DEVMODE    - mode développeur (caché, activé par combinaison secrète)
 *
 * L'UI communique avec les démons système via IPC (sockets UNIX, JSON) :
 *   consoleos-gamemgrd  -> LIST / LAUNCH / INSTALL / UNINSTALL
 *   consoleos-padd      -> abonnement aux événements manette normalisés
 *   consoleos-notifyd   -> abonnement aux notifications (pastille discrète)
 *
 * Avant de lancer un jeu, l'UI libère ses ressources non essentielles
 * (textures de la bibliothèque, cache) afin de laisser un maximum de RAM
 * au jeu, conformément à l'objectif de performance du cahier des charges.
 *
 * Compilation : voir Makefile (SDL2, SDL2_image, SDL2_ttf, sqlite3)
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sqlite3.h>

#include "theme.h"
#include "ipc.h"

#define WINDOW_W 1280
#define WINDOW_H 720
#define MAX_GAMES 256
#define MAX_NOTIFICATIONS 5

typedef enum { VIEW_HOME, VIEW_GAME_INFO, VIEW_SETTINGS, VIEW_DEVMODE } view_t;

typedef struct {
    char id[128];
    char name[256];
    char version[32];
    int playtime;
    long last_played;
} game_entry_t;

typedef struct {
    char text[256];
    Uint32 shown_at;
} notification_t;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font_large;
    TTF_Font *font_medium;
    TTF_Font *font_small;
    theme_t theme;
    SDL_Texture *wallpaper;

    view_t view;
    game_entry_t games[MAX_GAMES];
    int n_games;
    int selected;

    notification_t notifications[MAX_NOTIFICATIONS];
    int n_notifications;

    int show_sysmonitor;   /* désactivé par défaut (cahier des charges) */
    int devmode_enabled;   /* désactivé par défaut */
    int running;

    int gamemgr_fd;
    int notify_fd;
    int pad_fd;
    int pad_lb_held;
    int pad_rb_held;
    int pad_start_held;
    int pad_devmode_combo_fired;
    int pad_dpad_x_prev;
    int pad_dpad_y_prev;
    int pad_lstick_x_prev;
    int pad_lstick_y_prev;
} app_t;

/* ---------------------------------------------------------------------- */
/* Rendu de texte utilitaire                                               */
/* ---------------------------------------------------------------------- */

static void draw_text(app_t *app, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    if (!font || !text || !text[0]) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(app->renderer, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(app->renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static void fill_rounded_rect(SDL_Renderer *r, SDL_Rect rect, SDL_Color c) {
    /* Approximation simple d'un rectangle à coins arrondis suffisante pour
     * une interface console (évite une dépendance à une lib vectorielle). */
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}

static void toggle_devmode(app_t *app);
static void launch_selected_game(app_t *app);

/* ---------------------------------------------------------------------- */
/* IPC : bibliothèque de jeux                                              */
/* ---------------------------------------------------------------------- */

static void refresh_game_list(app_t *app) {
    int fd = ipc_client_connect(CONSOLEOS_GAMEMGR_SOCK);
    if (fd < 0) {
        fprintf(stderr, "ui: impossible de joindre consoleos-gamemgrd\n");
        return;
    }
    ipc_send_line(fd, "LIST");

    app->n_games = 0;
    char buf[IPC_MAX_LINE];
    FILE *fp = fdopen(dup(fd), "r");
    while (fp && fgets(buf, sizeof(buf), fp)) {
        if (strstr(buf, "list_end")) break;
        if (strstr(buf, "list_begin")) continue;
        if (app->n_games >= MAX_GAMES) break;

        game_entry_t *g = &app->games[app->n_games];
        memset(g, 0, sizeof(*g));

        char *p;
        if ((p = strstr(buf, "\"id\":\""))) sscanf(p, "\"id\":\"%127[^\"]", g->id);
        if ((p = strstr(buf, "\"name\":\""))) sscanf(p, "\"name\":\"%255[^\"]", g->name);
        if ((p = strstr(buf, "\"version\":\""))) sscanf(p, "\"version\":\"%31[^\"]", g->version);
        if ((p = strstr(buf, "\"playtime\":"))) sscanf(p, "\"playtime\":%d", &g->playtime);
        if ((p = strstr(buf, "\"last_played\":"))) sscanf(p, "\"last_played\":%ld", &g->last_played);

        app->n_games++;
    }
    if (fp) fclose(fp);
    close(fd);
}

/* Libère toute mémoire/texture non essentielle avant de lancer un jeu,
 * afin de maximiser la RAM disponible pour celui-ci (cahier des charges). */
static void free_ui_resources_before_game(app_t *app) {
    if (app->wallpaper) { SDL_DestroyTexture(app->wallpaper); app->wallpaper = NULL; }
    /* Les textures de jaquettes/icônes seraient également libérées ici si
     * elles étaient conservées en cache ; ce squelette ne les garde pas
     * chargées en permanence, elles sont donc déjà minimisées. */
}

static void launch_selected_game(app_t *app) {
    if (app->selected < 0 || app->selected >= app->n_games) return;
    game_entry_t *g = &app->games[app->selected];

    free_ui_resources_before_game(app);

    int fd = ipc_client_connect(CONSOLEOS_GAMEMGR_SOCK);
    if (fd < 0) return;
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "LAUNCH %s", g->id);
    ipc_send_line(fd, cmd);
    char resp[256];
    read(fd, resp, sizeof(resp) - 1);
    close(fd);

    /* Le jeu s'exécute de façon synchrone côté gamemgrd (fork+waitpid) ;
     * on recharge simplement la bibliothèque et le fond d'écran au retour. */
    if (!app->theme.wallpaper_path[0]) return;
    SDL_Surface *surf = IMG_Load(app->theme.wallpaper_path);
    if (surf) {
        app->wallpaper = SDL_CreateTextureFromSurface(app->renderer, surf);
        SDL_FreeSurface(surf);
    }
    refresh_game_list(app);
}

/* ---------------------------------------------------------------------- */
/* Notifications (abonnement au bus consoleos-notifyd)                     */
/* ---------------------------------------------------------------------- */

static void push_notification(app_t *app, const char *text) {
    if (app->n_notifications >= MAX_NOTIFICATIONS) {
        memmove(&app->notifications[0], &app->notifications[1],
                sizeof(notification_t) * (MAX_NOTIFICATIONS - 1));
        app->n_notifications--;
    }
    notification_t *n = &app->notifications[app->n_notifications++];
    strncpy(n->text, text, sizeof(n->text) - 1);
    n->shown_at = SDL_GetTicks();
}

static void poll_notifications(app_t *app) {
    if (app->notify_fd < 0) return;
    char buf[IPC_MAX_LINE];
    ssize_t n = recv(app->notify_fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    if (n > 0) {
        buf[n] = 0;
        if (strstr(buf, "\"name\":\"")) {
            handle_pad_message(app, buf);
        } else {
            char text[256] = {0};
            char *p = strstr(buf, "\"text\":\"");
            if (p) sscanf(p, "\"text\":\"%255[^\"]", text);
            if (text[0]) push_notification(app, text);
        }
    }
}

/* Purge les notifications affichées depuis plus de 4 secondes */
static void expire_notifications(app_t *app) {
    Uint32 now = SDL_GetTicks();
    int w = 0;
    for (int i = 0; i < app->n_notifications; i++) {
        if (now - app->notifications[i].shown_at < 4000) {
            app->notifications[w++] = app->notifications[i];
        }
    }
    app->n_notifications = w;
}

/* ---------------------------------------------------------------------- */
/* Mode développeur (caché, jamais visible en usage normal)                */
/* ---------------------------------------------------------------------- */

/* Combinaison secrète : LB+RB+START maintenus, détectée au niveau clavier
 * ici via une touche de test (F12) puisque le mapping manette réel passe
 * par consoleos-padd ; en usage manette, la même logique s'applique aux
 * événements normalisés reçus sur le socket pad. */
static void toggle_devmode(app_t *app) {
    app->devmode_enabled = !app->devmode_enabled;
    fprintf(stderr, "ui: mode développeur %s\n", app->devmode_enabled ? "ACTIVÉ" : "désactivé");
    push_notification(app, app->devmode_enabled ? "Mode développeur activé" : "Mode développeur désactivé");
}

/* ---------------------------------------------------------------------- */
/* Rendu des vues                                                          */
/* ---------------------------------------------------------------------- */

static void render_home(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, app->theme.bg.r, app->theme.bg.g, app->theme.bg.b, 255);
    SDL_RenderClear(app->renderer);

    if (app->wallpaper) {
        SDL_RenderCopy(app->renderer, app->wallpaper, NULL, NULL);
    }

    draw_text(app, app->font_large, "ConsoleOS", 40, 30, app->theme.text);

    if (app->n_games == 0) {
        draw_text(app, app->font_medium,
            "Aucun jeu installé — branchez une clé USB avec un paquet .zpk",
            40, 120, app->theme.text_dim);
    }

    int cols = 5;
    int cell_w = 220, cell_h = 260, gap = 24;
    int start_x = 40, start_y = 120;

    for (int i = 0; i < app->n_games; i++) {
        int col = i % cols, row = i / cols;
        SDL_Rect cell = { start_x + col * (cell_w + gap), start_y + row * (cell_h + gap), cell_w, cell_h };

        SDL_Color panel = (i == app->selected) ? app->theme.highlight : app->theme.panel;
        fill_rounded_rect(app->renderer, cell, panel);

        draw_text(app, app->font_small, app->games[i].name, cell.x + 10, cell.y + cell_h - 40, app->theme.text);
    }

    /* Barre de statut système (désactivée par défaut, cahier des charges) */
    if (app->show_sysmonitor) {
        char status[128];
        snprintf(status, sizeof(status), "FPS: 60 | CPU: -- %% | RAM UI: -- Mo");
        draw_text(app, app->font_small, status, WINDOW_W - 340, 10, app->theme.text_dim);
    }

    /* Notifications discrètes en bas à droite */
    int ny = WINDOW_H - 50;
    for (int i = app->n_notifications - 1; i >= 0; i--) {
        SDL_Rect box = { WINDOW_W - 360, ny, 340, 40 };
        fill_rounded_rect(app->renderer, box, app->theme.panel);
        draw_text(app, app->font_small, app->notifications[i].text, box.x + 12, box.y + 10, app->theme.text);
        ny -= 48;
    }
}

static void render_devmode(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Mode développeur", 40, 30, app->theme.accent);
    draw_text(app, app->font_medium, "Gestionnaire de fichiers | Terminal | Journaux | Réseau | Benchmarks | Test manettes",
               40, 100, app->theme.text);
    draw_text(app, app->font_small, "(Échap pour revenir à la bibliothèque)", 40, 140, app->theme.text_dim);
}

static void render_settings(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, app->theme.bg.r, app->theme.bg.g, app->theme.bg.b, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Paramètres", 40, 30, app->theme.text);
    draw_text(app, app->font_medium, "Thème, couleurs, fond d'écran, sons, disposition",
               40, 100, app->theme.text_dim);
}

/* ---------------------------------------------------------------------- */
/* Boucle principale                                                       */
/* ---------------------------------------------------------------------- */

static void nav_move(app_t *app, int dx, int dy) {
    if (app->view != VIEW_HOME || app->n_games <= 0) return;
    int n = app->n_games;
    if (dx > 0) app->selected = (app->selected + 1) % n;
    else if (dx < 0) app->selected = (app->selected - 1 + n) % n;
    else if (dy > 0) app->selected = (app->selected + 5) % n;
    else if (dy < 0) app->selected = (app->selected - 5 + n) % n;
}

static void nav_select(app_t *app) {
    if (app->view == VIEW_HOME) launch_selected_game(app);
}

static void nav_back(app_t *app) {
    if (app->view == VIEW_SETTINGS || app->view == VIEW_DEVMODE) app->view = VIEW_HOME;
}

static void nav_open_settings(app_t *app) {
    if (app->view == VIEW_HOME) app->view = VIEW_SETTINGS;
}

static void handle_keydown(app_t *app, SDL_Keycode key) {
    switch (key) {
        case SDLK_RIGHT:  nav_move(app, 1, 0); break;
        case SDLK_LEFT:   nav_move(app, -1, 0); break;
        case SDLK_DOWN:   nav_move(app, 0, 1); break;
        case SDLK_UP:     nav_move(app, 0, -1); break;
        case SDLK_RETURN: nav_select(app); break;
        case SDLK_ESCAPE: nav_back(app); break;
        case SDLK_F1:     nav_open_settings(app); break;
        case SDLK_F12:
            toggle_devmode(app);
            if (app->devmode_enabled) app->view = VIEW_DEVMODE;
            break;
        default: break;
    }
}

/* Traduit un événement manette normalisé (relayé par consoleos-padd via le
 * bus de notifications) en action de navigation. Voir docs/ipc-protocol.md
 * pour le format JSON des événements manette. */
static void handle_pad_message(app_t *app, const char *buf) {
    char type[16] = {0};
    char name[32] = {0};
    int value = 0;
    char *p;

    if ((p = strstr(buf, "\"type\":\""))) sscanf(p, "\"type\":\"%15[^\"]", type);
    if ((p = strstr(buf, "\"name\":\""))) sscanf(p, "\"name\":\"%31[^\"]", name);
    if ((p = strstr(buf, "\"value\":"))) sscanf(p, "\"value\":%d", &value);

    if (strcmp(type, "button") == 0) {
        int pressed = (value != 0);

        if (strcmp(name, "A") == 0 && pressed) nav_select(app);
        else if (strcmp(name, "B") == 0 && pressed) nav_back(app);
        else if (strcmp(name, "START") == 0 && pressed) nav_open_settings(app);

        if (strcmp(name, "LB") == 0) app->pad_lb_held = pressed;
        else if (strcmp(name, "RB") == 0) app->pad_rb_held = pressed;
        else if (strcmp(name, "START") == 0) app->pad_start_held = pressed;

        /* Combinaison secrète LB+RB+START maintenus -> mode développeur
         * (cahier des charges : jamais visible en usage normal). */
        if (app->pad_lb_held && app->pad_rb_held && app->pad_start_held) {
            if (!app->pad_devmode_combo_fired) {
                toggle_devmode(app);
                if (app->devmode_enabled) app->view = VIEW_DEVMODE;
                app->pad_devmode_combo_fired = 1;
            }
        } else {
            app->pad_devmode_combo_fired = 0;
        }
    } else if (strcmp(type, "axis") == 0) {
        const int THRESHOLD = 20000; /* marge pour ignorer le bruit du stick au repos */

        if (strcmp(name, "DPAD_X") == 0) {
            if (value < 0 && app->pad_dpad_x_prev >= 0) nav_move(app, -1, 0);
            else if (value > 0 && app->pad_dpad_x_prev <= 0) nav_move(app, 1, 0);
            app->pad_dpad_x_prev = value;
        } else if (strcmp(name, "DPAD_Y") == 0) {
            if (value < 0 && app->pad_dpad_y_prev >= 0) nav_move(app, 0, -1);
            else if (value > 0 && app->pad_dpad_y_prev <= 0) nav_move(app, 0, 1);
            app->pad_dpad_y_prev = value;
        } else if (strcmp(name, "LSTICK_X") == 0) {
            if (value > THRESHOLD && app->pad_lstick_x_prev <= THRESHOLD) nav_move(app, 1, 0);
            else if (value < -THRESHOLD && app->pad_lstick_x_prev >= -THRESHOLD) nav_move(app, -1, 0);
            app->pad_lstick_x_prev = value;
        } else if (strcmp(name, "LSTICK_Y") == 0) {
            if (value > THRESHOLD && app->pad_lstick_y_prev <= THRESHOLD) nav_move(app, 0, 1);
            else if (value < -THRESHOLD && app->pad_lstick_y_prev >= -THRESHOLD) nav_move(app, 0, -1);
            app->pad_lstick_y_prev = value;
        }
    }
}

int main(int argc, char **argv) {
    char theme_dir[512] = "/usr/share/consoleos/themes/default";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--theme") == 0 && i + 1 < argc) strncpy(theme_dir, argv[++i], sizeof(theme_dir)-1);
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "ui: SDL_Init a échoué : %s\n", SDL_GetError());
        return 1;
    }
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);

    app_t app;
    memset(&app, 0, sizeof(app));
    app.running = 1;
    app.view = VIEW_HOME;
    app.selected = 0;
    app.show_sysmonitor = 0;   /* désactivé par défaut */
    app.devmode_enabled = 0;   /* désactivé par défaut */

    app.window = SDL_CreateWindow("ConsoleOS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    theme_load(theme_dir, &app.theme);

    if (app.theme.font_path[0]) {
        app.font_large  = TTF_OpenFont(app.theme.font_path, 42);
        app.font_medium = TTF_OpenFont(app.theme.font_path, 24);
        app.font_small  = TTF_OpenFont(app.theme.font_path, 16);
    }
    if (app.theme.wallpaper_path[0]) {
        SDL_Surface *surf = IMG_Load(app.theme.wallpaper_path);
        if (surf) { app.wallpaper = SDL_CreateTextureFromSurface(app.renderer, surf); SDL_FreeSurface(surf); }
    }

    app.gamemgr_fd = -1;
    app.notify_fd = ipc_client_connect(CONSOLEOS_NOTIFY_SOCK);
    if (app.notify_fd >= 0) ipc_send_line(app.notify_fd, "SUBSCRIBE");
    app.pad_fd = -1;

    refresh_game_list(&app);

    Uint32 frame_start;
    const int target_frame_ms = 1000 / 60; /* 60 FPS minimum, cahier des charges */

    while (app.running) {
        frame_start = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) app.running = 0;
            else if (ev.type == SDL_KEYDOWN) handle_keydown(&app, ev.key.keysym.sym);
        }

        poll_notifications(&app);
        expire_notifications(&app);

        switch (app.view) {
            case VIEW_HOME: render_home(&app); break;
            case VIEW_SETTINGS: render_settings(&app); break;
            case VIEW_DEVMODE: render_devmode(&app); break;
            default: render_home(&app); break;
        }

        SDL_RenderPresent(app.renderer);

        Uint32 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < (Uint32)target_frame_ms) SDL_Delay(target_frame_ms - elapsed);
    }

    if (app.notify_fd >= 0) close(app.notify_fd);
    if (app.wallpaper) SDL_DestroyTexture(app.wallpaper);
    if (app.font_large) TTF_CloseFont(app.font_large);
    if (app.font_medium) TTF_CloseFont(app.font_medium);
    if (app.font_small) TTF_CloseFont(app.font_small);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
