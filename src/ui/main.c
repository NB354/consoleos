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
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sqlite3.h>

#include "theme.h"
#include "ipc.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <arpa/inet.h>

#define WINDOW_W 1280
#define WINDOW_H 720
#define MAX_GAMES 256
#define MAX_NOTIFICATIONS 5

typedef enum {
    VIEW_HOME, VIEW_GAME_INFO, VIEW_SETTINGS, VIEW_DEVMODE,
    VIEW_DEV_FILES, VIEW_DEV_LOGS, VIEW_DEV_HWINFO,
    VIEW_DEV_NETWORK, VIEW_DEV_BENCHMARK, VIEW_DEV_PADTEST
} view_t;

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
    Mix_Chunk *sound_nav;
    Mix_Chunk *sound_select;

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

    /* Gestionnaire de fichiers */
    char fm_path[512];
    char fm_entries[128][256];
    int fm_n_entries;
    int fm_selected;

    /* Journaux système */
    char log_lines[200][200];
    int log_n_lines;
    int log_scroll;

    /* Réseau */
    char net_lines[16][200];
    int net_n_lines;

    /* Benchmarks */
    double bench_cpu_mops;
    double bench_mem_mbps;
    int bench_done;

    /* Test manettes */
    char pad_history[8][128];
    int pad_history_count;

    /* Paramètres */
    char theme_names[16][64];
    int n_themes;
    int settings_selected;

    /* Mode développeur : sélection dans le menu */
    int dev_selected;
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
static void handle_pad_message(app_t *app, const char *buf);

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

/* ==================== Informations système (utilitaires) ==================== */

static void read_meminfo(long *total_kb, long *free_kb) {
    *total_kb = 0; *free_kb = 0;
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %ld kB", total_kb) == 1) continue;
        sscanf(line, "MemAvailable: %ld kB", free_kb);
    }
    fclose(f);
}

static double read_loadavg(void) {
    double load = 0.0;
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) { fscanf(f, "%lf", &load); fclose(f); }
    return load;
}

static int read_cpu_temp_millic(void) {
    int t = 0;
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f) { fscanf(f, "%d", &t); fclose(f); }
    return t;
}

/* ==================== Paramètres : gestion des thèmes ==================== */

static void scan_themes(app_t *app) {
    app->n_themes = 0;
    DIR *d = opendir("/usr/share/consoleos/themes");
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && app->n_themes < 16) {
        if (ent->d_name[0] == '.') continue;
        char check[600];
        snprintf(check, sizeof(check), "/usr/share/consoleos/themes/%s/theme.json", ent->d_name);
        FILE *f = fopen(check, "r");
        if (!f) continue;
        fclose(f);
        strncpy(app->theme_names[app->n_themes], ent->d_name, 63);
        app->n_themes++;
    }
    closedir(d);
}

/* Recharge un thème en direct : couleurs, police, fond d'écran, sons. */
static void apply_theme(app_t *app, const char *name) {
    char dir[500];
    snprintf(dir, sizeof(dir), "/usr/share/consoleos/themes/%s", name);
    theme_load(dir, &app->theme);

    if (app->font_large) TTF_CloseFont(app->font_large);
    if (app->font_medium) TTF_CloseFont(app->font_medium);
    if (app->font_small) TTF_CloseFont(app->font_small);
    app->font_large = app->font_medium = app->font_small = NULL;
    if (app->theme.font_path[0]) {
        app->font_large  = TTF_OpenFont(app->theme.font_path, 42);
        app->font_medium = TTF_OpenFont(app->theme.font_path, 24);
        app->font_small  = TTF_OpenFont(app->theme.font_path, 16);
    }

    if (app->wallpaper) { SDL_DestroyTexture(app->wallpaper); app->wallpaper = NULL; }
    if (app->theme.wallpaper_path[0]) {
        SDL_Surface *surf = IMG_Load(app->theme.wallpaper_path);
        if (surf) { app->wallpaper = SDL_CreateTextureFromSurface(app->renderer, surf); SDL_FreeSurface(surf); }
    }

    if (app->sound_nav) { Mix_FreeChunk(app->sound_nav); app->sound_nav = NULL; }
    if (app->sound_select) { Mix_FreeChunk(app->sound_select); app->sound_select = NULL; }
    if (app->theme.sound_nav[0]) app->sound_nav = Mix_LoadWAV(app->theme.sound_nav);
    if (app->theme.sound_select[0]) app->sound_select = Mix_LoadWAV(app->theme.sound_select);

    /* Retient le choix pour le prochain démarrage */
    FILE *f = fopen("/etc/consoleos/theme.conf", "w");
    if (f) { fprintf(f, "%s\n", name); fclose(f); }
}

/* ==================== Gestionnaire de fichiers ==================== */
/* Lecture seule, en dehors de tout bac à sable : outil de diagnostic
 * réservé au mode développeur, jamais accessible en usage normal. */

static void fm_load_dir(app_t *app, const char *path) {
    strncpy(app->fm_path, path, sizeof(app->fm_path) - 1);
    app->fm_n_entries = 0;
    app->fm_selected = 0;
    if (strcmp(path, "/") != 0) {
        strncpy(app->fm_entries[app->fm_n_entries++], "..", sizeof(app->fm_entries[0]));
    }
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && app->fm_n_entries < 128) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char full[600];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        struct stat st;
        int is_dir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
        snprintf(app->fm_entries[app->fm_n_entries], sizeof(app->fm_entries[0]),
                 "%s%s", ent->d_name, is_dir ? "/" : "");
        app->fm_n_entries++;
    }
    closedir(d);
}

static void fm_enter_selected(app_t *app) {
    if (app->fm_selected < 0 || app->fm_selected >= app->fm_n_entries) return;
    char *name = app->fm_entries[app->fm_selected];

    if (strcmp(name, "..") == 0) {
        char *last_slash = strrchr(app->fm_path, '/');
        if (last_slash && last_slash != app->fm_path) *last_slash = 0;
        else strcpy(app->fm_path, "/");
        fm_load_dir(app, app->fm_path);
        return;
    }
    size_t len = strlen(name);
    if (len > 0 && name[len - 1] == '/') {
        char clean[256], newpath[600];
        strncpy(clean, name, sizeof(clean) - 1);
        clean[len - 1] = 0;
        if (strcmp(app->fm_path, "/") == 0) snprintf(newpath, sizeof(newpath), "/%s", clean);
        else snprintf(newpath, sizeof(newpath), "%s/%s", app->fm_path, clean);
        fm_load_dir(app, newpath);
    }
}

static void render_dev_files(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Gestionnaire de fichiers", 40, 20, app->theme.accent);
    draw_text(app, app->font_small, app->fm_path, 40, 65, app->theme.text_dim);

    int y = 100, visible = 18;
    int start = (app->fm_selected / visible) * visible;
    for (int i = start; i < app->fm_n_entries && i < start + visible; i++) {
        SDL_Color c = (i == app->fm_selected) ? app->theme.highlight : app->theme.text;
        char line[300];
        snprintf(line, sizeof(line), "%s %s", (i == app->fm_selected) ? ">" : " ", app->fm_entries[i]);
        draw_text(app, app->font_small, line, 60, y, c);
        y += 24;
    }
    if (app->fm_n_entries == 0) draw_text(app, app->font_small, "(dossier vide ou illisible)", 60, y, app->theme.text_dim);
    draw_text(app, app->font_small, "Entrée : ouvrir | Échap : dossier parent",
               40, WINDOW_H - 30, app->theme.text_dim);
}

/* ==================== Journaux système ==================== */

static void load_system_logs(app_t *app) {
    app->log_n_lines = 0;
    FILE *f = popen("dmesg | tail -n 200", "r");
    if (!f) return;
    char line[200];
    while (fgets(line, sizeof(line), f) && app->log_n_lines < 200) {
        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\n') line[l - 1] = 0;
        strncpy(app->log_lines[app->log_n_lines], line, sizeof(app->log_lines[0]) - 1);
        app->log_n_lines++;
    }
    pclose(f);
    app->log_scroll = app->log_n_lines > 20 ? app->log_n_lines - 20 : 0;
}

static void render_dev_logs(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Journaux système (dmesg)", 40, 20, app->theme.accent);
    draw_text(app, app->font_small, "Haut/Bas : défiler | Échap : retour", 40, 65, app->theme.text_dim);
    int y = 100;
    for (int i = app->log_scroll; i < app->log_n_lines && y < WINDOW_H - 20; i++) {
        draw_text(app, app->font_small, app->log_lines[i], 40, y, app->theme.text);
        y += 20;
    }
}

/* ==================== Informations matérielles ==================== */

static void render_dev_hwinfo(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Informations matérielles", 40, 20, app->theme.accent);

    char cpu_model[128] = "inconnu";
    int n_cores = 0;
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "model name", 10) == 0 || strncmp(line, "Model", 5) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    strncpy(cpu_model, colon + 2, sizeof(cpu_model) - 1);
                    size_t l = strlen(cpu_model);
                    if (l && cpu_model[l - 1] == '\n') cpu_model[l - 1] = 0;
                }
            }
            if (strncmp(line, "processor", 9) == 0) n_cores++;
        }
        fclose(f);
    }

    long total_kb, free_kb;
    read_meminfo(&total_kb, &free_kb);
    int temp_c = read_cpu_temp_millic() / 1000;

    struct statvfs vfs;
    long storage_free_mb = 0, storage_total_mb = 0;
    if (statvfs("/", &vfs) == 0) {
        storage_total_mb = (long)((double)vfs.f_blocks * vfs.f_frsize / (1024 * 1024));
        storage_free_mb  = (long)((double)vfs.f_bavail * vfs.f_frsize / (1024 * 1024));
    }

    int y = 80;
    char line[256];
    snprintf(line, sizeof(line), "CPU : %s", cpu_model);
    draw_text(app, app->font_medium, line, 40, y, app->theme.text); y += 34;
    snprintf(line, sizeof(line), "Cœurs : %d", n_cores);
    draw_text(app, app->font_medium, line, 40, y, app->theme.text); y += 34;
    snprintf(line, sizeof(line), "Température CPU : %d°C", temp_c);
    draw_text(app, app->font_medium, line, 40, y, app->theme.text); y += 34;
    snprintf(line, sizeof(line), "RAM : %ld / %ld Mo libres", free_kb / 1024, total_kb / 1024);
    draw_text(app, app->font_medium, line, 40, y, app->theme.text); y += 34;
    snprintf(line, sizeof(line), "Stockage : %ld / %ld Mo libres", storage_free_mb, storage_total_mb);
    draw_text(app, app->font_medium, line, 40, y, app->theme.text);
}

/* ==================== Outils réseau ==================== */

static void load_network_info(app_t *app) {
    app->net_n_lines = 0;
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL && app->net_n_lines < 15; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            char host[NI_MAXHOST];
            getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, sizeof(host), NULL, 0, NI_NUMERICHOST);
            snprintf(app->net_lines[app->net_n_lines], sizeof(app->net_lines[0]), "%s : %s", ifa->ifa_name, host);
            app->net_n_lines++;
        }
        freeifaddrs(ifaddr);
    }
    if (app->net_n_lines == 0) {
        strncpy(app->net_lines[0], "(aucune interface active)", sizeof(app->net_lines[0]) - 1);
        app->net_n_lines = 1;
    }
}

static void render_dev_network(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Outils réseau", 40, 20, app->theme.accent);
    int y = 80;
    draw_text(app, app->font_medium, "Interfaces :", 40, y, app->theme.text_dim); y += 32;
    for (int i = 0; i < app->net_n_lines; i++) {
        draw_text(app, app->font_small, app->net_lines[i], 60, y, app->theme.text);
        y += 24;
    }
    draw_text(app, app->font_small, "Entrée : tester la connexion internet (ping)",
               40, WINDOW_H - 30, app->theme.text_dim);
}

/* ==================== Benchmarks ==================== */

static void run_benchmarks(app_t *app) {
    struct timespec t0, t1;
    volatile unsigned long acc = 0;
    unsigned long iters = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    do {
        for (int i = 0; i < 100000; i++) acc += (unsigned long)i * 2654435761u;
        iters += 100000;
        clock_gettime(CLOCK_MONOTONIC, &t1);
    } while ((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000 < 300);
    double cpu_s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    app->bench_cpu_mops = (iters / 1000000.0) / cpu_s;

    size_t bufsize = 16 * 1024 * 1024;
    void *src = malloc(bufsize), *dst = malloc(bufsize);
    if (src && dst) {
        memset(src, 0xAA, bufsize);
        int copies = 0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        do {
            memcpy(dst, src, bufsize);
            copies++;
            clock_gettime(CLOCK_MONOTONIC, &t1);
        } while ((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000 < 300);
        double mem_s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
        app->bench_mem_mbps = (copies * bufsize / (1024.0 * 1024.0)) / mem_s;
    }
    free(src); free(dst);
    app->bench_done = 1;
}

static void render_dev_benchmark(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Benchmarks", 40, 20, app->theme.accent);
    if (!app->bench_done) {
        draw_text(app, app->font_medium, "Entrée : lancer le test (~1 seconde)", 40, 90, app->theme.text_dim);
        return;
    }
    char line[128];
    snprintf(line, sizeof(line), "CPU : %.1f millions d'opérations / seconde", app->bench_cpu_mops);
    draw_text(app, app->font_medium, line, 40, 90, app->theme.text);
    snprintf(line, sizeof(line), "Mémoire : %.0f Mo/s (memcpy)", app->bench_mem_mbps);
    draw_text(app, app->font_medium, line, 40, 130, app->theme.text);
    draw_text(app, app->font_small, "Entrée : relancer le test", 40, 180, app->theme.text_dim);
}

/* ==================== Test des manettes ==================== */

static void render_dev_padtest(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Test des manettes", 40, 20, app->theme.accent);
    draw_text(app, app->font_small, "Appuyez sur les boutons ou bougez les sticks",
               40, 65, app->theme.text_dim);
    int y = 110;
    for (int i = 0; i < app->pad_history_count; i++) {
        draw_text(app, app->font_small, app->pad_history[i], 60, y, app->theme.text);
        y += 24;
    }
    if (app->pad_history_count == 0) draw_text(app, app->font_small, "(aucun événement reçu)", 60, y, app->theme.text_dim);
}

/* Lance un vrai shell sur la console texte, puis rend la main au compositeur. */
static void open_dev_terminal(app_t *app) {
    SDL_MinimizeWindow(app->window);
    fprintf(stderr, "ui: terminal développeur ouvert (tapez 'exit' pour revenir)\n");
    system("/bin/sh");
    SDL_RestoreWindow(app->window);
    SDL_RaiseWindow(app->window);
}

static int test_internet_connectivity(void) {
    return system("ping -c 1 -W 1 8.8.8.8 > /dev/null 2>&1") == 0;
}

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

static void render_game_info(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, app->theme.bg.r, app->theme.bg.g, app->theme.bg.b, 255);
    SDL_RenderClear(app->renderer);

    if (app->selected < 0 || app->selected >= app->n_games) {
        draw_text(app, app->font_medium, "Aucun jeu sélectionné", 40, 30, app->theme.text_dim);
        return;
    }
    game_entry_t *g = &app->games[app->selected];

    draw_text(app, app->font_large, g->name, 40, 30, app->theme.text);

    char line[200];
    int y = 100;

    snprintf(line, sizeof(line), "Version : %s", g->version[0] ? g->version : "inconnue");
    draw_text(app, app->font_medium, line, 40, y, app->theme.text); y += 40;

    int hours = g->playtime / 3600;
    int minutes = (g->playtime % 3600) / 60;
    if (hours > 0) snprintf(line, sizeof(line), "Temps de jeu : %dh %02dmin", hours, minutes);
    else snprintf(line, sizeof(line), "Temps de jeu : %d min", minutes);
    draw_text(app, app->font_medium, line, 40, y, app->theme.text); y += 40;

    if (g->last_played > 0) {
        time_t t = (time_t)g->last_played;
        struct tm *tm_info = localtime(&t);
        char datebuf[64];
        strftime(datebuf, sizeof(datebuf), "%d/%m/%Y à %H:%M", tm_info);
        snprintf(line, sizeof(line), "Dernière utilisation : %s", datebuf);
    } else {
        snprintf(line, sizeof(line), "Dernière utilisation : jamais joué");
    }
    draw_text(app, app->font_medium, line, 40, y, app->theme.text); y += 40;

    snprintf(line, sizeof(line), "Identifiant : %s", g->id);
    draw_text(app, app->font_small, line, 40, y, app->theme.text_dim); y += 50;

    draw_text(app, app->font_small, "Entrée : lancer le jeu | Échap : retour à la bibliothèque",
               40, WINDOW_H - 40, app->theme.text_dim);
}

#define DEV_MENU_ITEMS 8
static const char *dev_menu_labels[DEV_MENU_ITEMS] = {
    "Ouvrir un terminal",
    "Journaux système",
    "Gestionnaire de fichiers",
    "Informations matérielles",
    "Outils réseau",
    "Benchmarks",
    "Test des manettes",
    "Quitter le mode développeur",
};

static void render_devmode(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Mode développeur", 40, 20, app->theme.accent);

    long total_kb, free_kb;
    read_meminfo(&total_kb, &free_kb);
    char info[200];
    snprintf(info, sizeof(info), "RAM : %ld / %ld Mo libres | Charge : %.2f | CPU : %d°C",
             free_kb / 1024, total_kb / 1024, read_loadavg(), read_cpu_temp_millic() / 1000);
    draw_text(app, app->font_small, info, 40, 70, app->theme.text_dim);

    int y = 120;
    for (int i = 0; i < DEV_MENU_ITEMS; i++) {
        SDL_Color c = (i == app->dev_selected) ? app->theme.highlight : app->theme.text;
        char line[100];
        snprintf(line, sizeof(line), "%s %s", (i == app->dev_selected) ? ">" : " ", dev_menu_labels[i]);
        draw_text(app, app->font_medium, line, 40, y, c);
        y += 36;
    }
}

static void render_settings(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, app->theme.bg.r, app->theme.bg.g, app->theme.bg.b, 255);
    SDL_RenderClear(app->renderer);
    draw_text(app, app->font_large, "Paramètres", 40, 30, app->theme.text);
    draw_text(app, app->font_small, "Haut/Bas pour naviguer, Entrée pour appliquer, Échap pour revenir",
               40, 90, app->theme.text_dim);

    int y = 140;
    draw_text(app, app->font_medium, "Thème :", 40, y, app->theme.text_dim);
    y += 36;
    for (int i = 0; i < app->n_themes; i++) {
        SDL_Color c = (i == app->settings_selected) ? app->theme.highlight : app->theme.text;
        char line[80];
        snprintf(line, sizeof(line), "%s  %s", (i == app->settings_selected) ? ">" : " ", app->theme_names[i]);
        draw_text(app, app->font_small, line, 60, y, c);
        y += 28;
    }

    y += 20;
    SDL_Color sysc = (app->settings_selected == app->n_themes) ? app->theme.highlight : app->theme.text;
    char sysline[80];
    snprintf(sysline, sizeof(sysline), "%s Affichage FPS/température : %s",
             (app->settings_selected == app->n_themes) ? ">" : " ",
             app->show_sysmonitor ? "activé" : "désactivé");
    draw_text(app, app->font_small, sysline, 40, y, sysc);
}

/* ---------------------------------------------------------------------- */
/* Boucle principale                                                       */
/* ---------------------------------------------------------------------- */

static void play_sound(Mix_Chunk *chunk) {
    if (chunk) Mix_PlayChannel(-1, chunk, 0);
}

static void nav_move(app_t *app, int dx, int dy) {
    if (app->view == VIEW_HOME && app->n_games > 0) {
        int n = app->n_games;
        if (dx > 0) app->selected = (app->selected + 1) % n;
        else if (dx < 0) app->selected = (app->selected - 1 + n) % n;
        else if (dy > 0) app->selected = (app->selected + 5) % n;
        else if (dy < 0) app->selected = (app->selected - 5 + n) % n;
        play_sound(app->sound_nav);
    } else if (app->view == VIEW_SETTINGS) {
        int n = app->n_themes + 1;
        if (dy > 0) app->settings_selected = (app->settings_selected + 1) % n;
        else if (dy < 0) app->settings_selected = (app->settings_selected - 1 + n) % n;
        play_sound(app->sound_nav);
    } else if (app->view == VIEW_DEVMODE) {
        if (dy > 0) app->dev_selected = (app->dev_selected + 1) % DEV_MENU_ITEMS;
        else if (dy < 0) app->dev_selected = (app->dev_selected - 1 + DEV_MENU_ITEMS) % DEV_MENU_ITEMS;
        play_sound(app->sound_nav);
    } else if (app->view == VIEW_DEV_FILES) {
        if (dy > 0 && app->fm_selected < app->fm_n_entries - 1) app->fm_selected++;
        else if (dy < 0 && app->fm_selected > 0) app->fm_selected--;
        play_sound(app->sound_nav);
    } else if (app->view == VIEW_DEV_LOGS) {
        if (dy > 0 && app->log_scroll < app->log_n_lines - 1) app->log_scroll++;
        else if (dy < 0 && app->log_scroll > 0) app->log_scroll--;
    }
}

static void nav_select(app_t *app) {
    if (app->view == VIEW_HOME || app->view == VIEW_GAME_INFO) {
        play_sound(app->sound_select);
        launch_selected_game(app);
    } else if (app->view == VIEW_SETTINGS) {
        play_sound(app->sound_select);
        if (app->settings_selected < app->n_themes) apply_theme(app, app->theme_names[app->settings_selected]);
        else app->show_sysmonitor = !app->show_sysmonitor;
    } else if (app->view == VIEW_DEVMODE) {
        play_sound(app->sound_select);
        switch (app->dev_selected) {
            case 0: open_dev_terminal(app); break;
            case 1: load_system_logs(app); app->view = VIEW_DEV_LOGS; break;
            case 2: fm_load_dir(app, "/"); app->view = VIEW_DEV_FILES; break;
            case 3: app->view = VIEW_DEV_HWINFO; break;
            case 4: load_network_info(app); app->view = VIEW_DEV_NETWORK; break;
            case 5: app->bench_done = 0; app->view = VIEW_DEV_BENCHMARK; break;
            case 6: app->pad_history_count = 0; app->view = VIEW_DEV_PADTEST; break;
            case 7: app->view = VIEW_HOME; break;
        }
    } else if (app->view == VIEW_DEV_FILES) {
        fm_enter_selected(app);
    } else if (app->view == VIEW_DEV_NETWORK) {
        int ok = test_internet_connectivity();
        int idx = app->net_n_lines < 15 ? app->net_n_lines : 15;
        snprintf(app->net_lines[idx], sizeof(app->net_lines[0]), "Test internet : %s", ok ? "OK" : "échec");
        if (app->net_n_lines < 16) app->net_n_lines++;
    } else if (app->view == VIEW_DEV_BENCHMARK) {
        run_benchmarks(app);
    }
}

static void nav_back(app_t *app) {
    if (app->view == VIEW_SETTINGS || app->view == VIEW_DEVMODE || app->view == VIEW_GAME_INFO) {
        app->view = VIEW_HOME;
    } else if (app->view == VIEW_DEV_FILES) {
        if (strcmp(app->fm_path, "/") == 0) {
            app->view = VIEW_DEVMODE;
        } else {
            char *last_slash = strrchr(app->fm_path, '/');
            if (last_slash && last_slash != app->fm_path) *last_slash = 0;
            else strcpy(app->fm_path, "/");
            fm_load_dir(app, app->fm_path);
        }
    } else if (app->view == VIEW_DEV_LOGS || app->view == VIEW_DEV_HWINFO ||
               app->view == VIEW_DEV_NETWORK || app->view == VIEW_DEV_BENCHMARK ||
               app->view == VIEW_DEV_PADTEST) {
        app->view = VIEW_DEVMODE;
    }
}

static void nav_open_settings(app_t *app) {
    if (app->view == VIEW_HOME) app->view = VIEW_SETTINGS;
}

static void nav_open_game_info(app_t *app) {
    if (app->view == VIEW_HOME && app->n_games > 0) app->view = VIEW_GAME_INFO;
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
        case SDLK_F2:     nav_open_game_info(app); break;
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

    if (app->pad_history_count >= 8) {
        for (int i = 1; i < 8; i++) strncpy(app->pad_history[i - 1], app->pad_history[i], sizeof(app->pad_history[0]));
        app->pad_history_count = 7;
    }
    snprintf(app->pad_history[app->pad_history_count], sizeof(app->pad_history[0]), "%s %s = %d", type, name, value);
    app->pad_history_count++;

    if (strcmp(type, "button") == 0) {
        int pressed = (value != 0);

        if (strcmp(name, "A") == 0 && pressed) nav_select(app);
        else if (strcmp(name, "B") == 0 && pressed) nav_back(app);
        else if (strcmp(name, "START") == 0 && pressed) nav_open_settings(app);
        else if (strcmp(name, "Y") == 0 && pressed) nav_open_game_info(app);

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
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        fprintf(stderr, "ui: Mix_OpenAudio a échoué : %s (sons désactivés)\n", Mix_GetError());
    }
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
    if (!app.renderer) {
        fprintf(stderr, "ui: renderer accéléré indisponible (%s), bascule en rendu logiciel\n", SDL_GetError());
        app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!app.renderer) {
        fprintf(stderr, "ui: impossible de créer un renderer, arrêt (%s)\n", SDL_GetError());
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }

    theme_load(theme_dir, &app.theme);
    scan_themes(&app);

    if (app.theme.font_path[0]) {
        app.font_large  = TTF_OpenFont(app.theme.font_path, 42);
        app.font_medium = TTF_OpenFont(app.theme.font_path, 24);
        app.font_small  = TTF_OpenFont(app.theme.font_path, 16);
    }
    if (app.theme.wallpaper_path[0]) {
        SDL_Surface *surf = IMG_Load(app.theme.wallpaper_path);
        if (surf) { app.wallpaper = SDL_CreateTextureFromSurface(app.renderer, surf); SDL_FreeSurface(surf); }
    }
    if (app.theme.sound_nav[0]) app.sound_nav = Mix_LoadWAV(app.theme.sound_nav);
    if (app.theme.sound_select[0]) app.sound_select = Mix_LoadWAV(app.theme.sound_select);

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
            case VIEW_GAME_INFO: render_game_info(&app); break;
            case VIEW_SETTINGS: render_settings(&app); break;
            case VIEW_DEVMODE: render_devmode(&app); break;
            case VIEW_DEV_FILES: render_dev_files(&app); break;
            case VIEW_DEV_LOGS: render_dev_logs(&app); break;
            case VIEW_DEV_HWINFO: render_dev_hwinfo(&app); break;
            case VIEW_DEV_NETWORK: render_dev_network(&app); break;
            case VIEW_DEV_BENCHMARK: render_dev_benchmark(&app); break;
            case VIEW_DEV_PADTEST: render_dev_padtest(&app); break;
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
    if (app.sound_nav) Mix_FreeChunk(app.sound_nav);
    if (app.sound_select) Mix_FreeChunk(app.sound_select);
    Mix_CloseAudio();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}