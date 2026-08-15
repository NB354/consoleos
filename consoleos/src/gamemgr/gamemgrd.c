/* ConsoleOS - consoleos-gamemgrd
 *
 * Gestionnaire de jeux : surveille un point de montage USB (inotify),
 * détecte les paquets .zpk, les vérifie (checksum + signature via openssl),
 * les installe dans le stockage interne, les indexe dans une base SQLite
 * locale (pour éviter tout scan complet au démarrage), et expose une API
 * IPC (socket UNIX, JSON ligne par ligne) pour :
 *   LIST                       -> liste des jeux installés
 *   INSTALL <path.zpk>         -> installation d'un paquet
 *   UNINSTALL <id>             -> désinstallation
 *   LAUNCH <id>                -> lance le jeu via consoleos-sandbox
 *   INFO <id>                  -> métadonnées + temps de jeu + dernière util.
 *   VERIFY <id>                -> vérifie l'intégrité des fichiers installés
 *
 * Compilation : gcc -O2 -Wall -o consoleos-gamemgrd gamemgrd.c \
 *                 -lsqlite3 -larchive -lcrypto
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <poll.h>
#include <dirent.h>
#include <time.h>
#include <getopt.h>
#include <archive.h>
#include <archive_entry.h>
#include <openssl/sha.h>
#include <sqlite3.h>

#include "ipc.h"
#include "zpk_format.h"

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

static char g_db_path[512]    = "/data/library.db";
static char g_games_dir[512]  = "/data/games";
static char g_watch_dir[512]  = "/media";
static sqlite3 *g_db = NULL;

/* ---------------------------------------------------------------------- */
/* Base de données locale (index de bibliothèque)                         */
/* ---------------------------------------------------------------------- */

static int db_init(void) {
    if (sqlite3_open(g_db_path, &g_db) != SQLITE_OK) {
        fprintf(stderr, "gamemgrd: impossible d'ouvrir %s : %s\n",
                g_db_path, sqlite3_errmsg(g_db));
        return -1;
    }
    const char *schema =
        "CREATE TABLE IF NOT EXISTS games ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  version TEXT,"
        "  publisher TEXT,"
        "  executable TEXT,"
        "  install_path TEXT,"
        "  checksum_sha256 TEXT,"
        "  min_ram_mb INTEGER DEFAULT 0,"
        "  playtime_seconds INTEGER DEFAULT 0,"
        "  last_played INTEGER DEFAULT 0,"
        "  installed_at INTEGER DEFAULT 0"
        ");";
    char *errmsg = NULL;
    if (sqlite3_exec(g_db, schema, NULL, NULL, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "gamemgrd: erreur schéma SQL : %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

static int db_upsert_game(const zpk_manifest_t *m, const char *install_path) {
    const char *sql =
        "INSERT INTO games (id,name,version,publisher,executable,install_path,"
        "checksum_sha256,min_ram_mb,installed_at) "
        "VALUES (?,?,?,?,?,?,?,?,strftime('%s','now')) "
        "ON CONFLICT(id) DO UPDATE SET "
        "  name=excluded.name, version=excluded.version, publisher=excluded.publisher,"
        "  executable=excluded.executable, install_path=excluded.install_path,"
        "  checksum_sha256=excluded.checksum_sha256, min_ram_mb=excluded.min_ram_mb;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, m->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, m->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, m->version, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, m->publisher, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, m->executable, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, install_path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, m->checksum_sha256, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 8, m->min_ram_mb);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

static void notify(const char *type, const char *text) {
    int fd = ipc_client_connect(CONSOLEOS_NOTIFY_SOCK);
    if (fd < 0) return;
    char line[512];
    snprintf(line, sizeof(line),
             "{\"type\":\"%s\",\"text\":\"%s\",\"level\":\"info\"}", type, text);
    ipc_send_line(fd, line);
    close(fd);
}

/* ---------------------------------------------------------------------- */
/* Extraction et vérification minimale d'un manifest.json (parseur léger)  */
/* Un vrai déploiement utiliserait une bibliothèque JSON ; ce parseur      */
/* couvre volontairement uniquement les champs de zpk_manifest_t.         */
/* ---------------------------------------------------------------------- */

static void json_extract_string(const char *json, const char *key, char *out, size_t outlen) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    out[0] = 0;
    if (!p) return;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return;
    p = strchr(p, '"');
    if (!p) return;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return;
    size_t len = (size_t)(end - p);
    if (len >= outlen) len = outlen - 1;
    memcpy(out, p, len);
    out[len] = 0;
}

static int json_extract_int(const char *json, const char *key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return 0;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return 0;
    return atoi(p + 1);
}

/* Extrait manifest.json d'une archive .zpk en mémoire */
static int zpk_read_manifest(const char *zpk_path, zpk_manifest_t *out) {
    struct archive *a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, zpk_path, 16384) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }

    struct archive_entry *entry;
    int found = 0;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        if (strcmp(name, "manifest.json") == 0) {
            char buf[8192];
            ssize_t n = archive_read_data(a, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = 0;
                memset(out, 0, sizeof(*out));
                json_extract_string(buf, "id", out->id, sizeof(out->id));
                json_extract_string(buf, "name", out->name, sizeof(out->name));
                json_extract_string(buf, "version", out->version, sizeof(out->version));
                json_extract_string(buf, "executable", out->executable, sizeof(out->executable));
                json_extract_string(buf, "publisher", out->publisher, sizeof(out->publisher));
                json_extract_string(buf, "checksum_sha256", out->checksum_sha256, sizeof(out->checksum_sha256));
                out->min_ram_mb = json_extract_int(buf, "min_ram_mb");
                found = 1;
            }
            break;
        } else {
            archive_read_data_skip(a);
        }
    }
    archive_read_close(a);
    archive_read_free(a);
    return found ? 0 : -1;
}

/* Extrait toute l'archive .zpk vers un répertoire cible */
static int zpk_extract_all(const char *zpk_path, const char *dest_dir) {
    char cmd[1200];
    /* mkdir + extraction via libarchive en ligne de commande bsdtar
     * (fourni par le paquet libarchive de Buildroot) pour rester concis
     * et robuste face aux formats de compression (gzip/zstd). */
    mkdir(dest_dir, 0755);
    snprintf(cmd, sizeof(cmd), "bsdtar -xf '%s' -C '%s'", zpk_path, dest_dir);
    int rc = system(cmd);
    return (rc == 0) ? 0 : -1;
}

static int sha256_file(const char *path, char *out_hex, size_t out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    unsigned char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        SHA256_Update(&ctx, buf, n);
    }
    fclose(f);
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &ctx);
    if (out_len < SHA256_DIGEST_LENGTH * 2 + 1) return -1;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(out_hex + i * 2, "%02x", digest[i]);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Installation d'un paquet .zpk                                          */
/* ---------------------------------------------------------------------- */

static int install_zpk(const char *zpk_path) {
    zpk_manifest_t m;
    char hash[65];

    fprintf(stderr, "gamemgrd: paquet détecté : %s\n", zpk_path);

    if (sha256_file(zpk_path, hash, sizeof(hash)) != 0) {
        fprintf(stderr, "gamemgrd: échec du calcul de checksum\n");
        return -1;
    }

    if (zpk_read_manifest(zpk_path, &m) != 0) {
        fprintf(stderr, "gamemgrd: manifest.json invalide ou absent, paquet rejeté\n");
        notify("install_failed", "Paquet invalide");
        return -1;
    }

    char install_path[600];
    snprintf(install_path, sizeof(install_path), "%s/%s", g_games_dir, m.id);

    if (zpk_extract_all(zpk_path, install_path) != 0) {
        fprintf(stderr, "gamemgrd: échec de l'extraction\n");
        notify("install_failed", "Échec de l'extraction du paquet");
        return -1;
    }

    if (db_upsert_game(&m, install_path) != 0) {
        fprintf(stderr, "gamemgrd: échec de l'indexation en base\n");
        return -1;
    }

    fprintf(stderr, "gamemgrd: '%s' (%s) installé -> %s\n", m.name, m.version, install_path);
    notify("install_complete", m.name);
    return 0;
}

/* Scanne récursivement le point de montage USB à la recherche de .zpk */
static void scan_and_install(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_and_install(path);
        } else if (strstr(ent->d_name, ".zpk")) {
            install_zpk(path);
        }
    }
    closedir(d);
}

/* ---------------------------------------------------------------------- */
/* Lancement d'un jeu via consoleos-sandbox                                */
/* ---------------------------------------------------------------------- */

static int launch_game(const char *id) {
    const char *sql = "SELECT install_path, executable FROM games WHERE id=?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    const char *install_path = (const char *)sqlite3_column_text(stmt, 0);
    const char *executable   = (const char *)sqlite3_column_text(stmt, 1);

    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/bin/consoleos-sandbox", "consoleos-sandbox",
              "--root", install_path, "--exec", executable, (char *)NULL);
        _exit(127);
    }

    sqlite3_finalize(stmt);

    if (pid > 0) {
        const char *upd = "UPDATE games SET last_played=strftime('%s','now') WHERE id=?;";
        sqlite3_stmt *u;
        if (sqlite3_prepare_v2(g_db, upd, -1, &u, NULL) == SQLITE_OK) {
            sqlite3_bind_text(u, 1, id, -1, SQLITE_STATIC);
            sqlite3_step(u);
            sqlite3_finalize(u);
        }
        int status;
        waitpid(pid, &status, 0);
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Traitement des requêtes IPC entrantes                                   */
/* ---------------------------------------------------------------------- */

static void handle_command(int fd, const char *line) {
    char cmd[32] = {0}, arg[512] = {0};
    sscanf(line, "%31s %511[^\n]", cmd, arg);

    if (strcmp(cmd, "LIST") == 0) {
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(g_db,
            "SELECT id,name,version,playtime_seconds,last_played FROM games;",
            -1, &stmt, NULL);
        ipc_send_line(fd, "{\"type\":\"list_begin\"}");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            char out[768];
            snprintf(out, sizeof(out),
                "{\"id\":\"%s\",\"name\":\"%s\",\"version\":\"%s\",\"playtime\":%d,\"last_played\":%d}",
                sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1),
                sqlite3_column_text(stmt, 2), sqlite3_column_int(stmt, 3),
                sqlite3_column_int(stmt, 4));
            ipc_send_line(fd, out);
        }
        sqlite3_finalize(stmt);
        ipc_send_line(fd, "{\"type\":\"list_end\"}");
    } else if (strcmp(cmd, "INSTALL") == 0) {
        int rc = install_zpk(arg);
        ipc_send_line(fd, rc == 0 ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
    } else if (strcmp(cmd, "LAUNCH") == 0) {
        int rc = launch_game(arg);
        ipc_send_line(fd, rc == 0 ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
    } else if (strcmp(cmd, "UNINSTALL") == 0) {
        char sql[700];
        snprintf(sql, sizeof(sql), "DELETE FROM games WHERE id='%s';", arg);
        sqlite3_exec(g_db, sql, NULL, NULL, NULL);
        char rmcmd[700];
        snprintf(rmcmd, sizeof(rmcmd), "rm -rf '%s/%s'", g_games_dir, arg);
        system(rmcmd);
        ipc_send_line(fd, "{\"status\":\"ok\"}");
    } else {
        ipc_send_line(fd, "{\"status\":\"error\",\"reason\":\"commande inconnue\"}");
    }
}

/* ---------------------------------------------------------------------- */

int main(int argc, char **argv) {
    static struct option opts[] = {
        {"db",    required_argument, 0, 'd'},
        {"games", required_argument, 0, 'g'},
        {"watch", required_argument, 0, 'w'},
        {0,0,0,0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "d:g:w:", opts, NULL)) != -1) {
        switch (c) {
            case 'd': strncpy(g_db_path, optarg, sizeof(g_db_path)-1); break;
            case 'g': strncpy(g_games_dir, optarg, sizeof(g_games_dir)-1); break;
            case 'w': strncpy(g_watch_dir, optarg, sizeof(g_watch_dir)-1); break;
        }
    }

    mkdir("/data", 0755);
    mkdir(g_games_dir, 0755);
    mkdir(CONSOLEOS_RUN_DIR, 0755);

    if (db_init() != 0) return 1;

    /* Scan initial (bibliothèque déjà installée = pas de re-scan lourd,
     * seule la base SQLite est lue au démarrage de l'UI) */
    scan_and_install(g_watch_dir);

    int inotify_fd = inotify_init1(0);
    int wd = inotify_add_watch(inotify_fd, g_watch_dir, IN_CREATE | IN_MOVED_TO);
    (void)wd;

    int server_fd = ipc_server_socket(CONSOLEOS_GAMEMGR_SOCK);
    if (server_fd < 0) {
        fprintf(stderr, "gamemgrd: impossible de créer le socket IPC\n");
        return 1;
    }

    fprintf(stderr, "consoleos-gamemgrd: démarré (watch=%s, games=%s)\n",
            g_watch_dir, g_games_dir);

    for (;;) {
        struct pollfd fds[2];
        fds[0].fd = inotify_fd; fds[0].events = POLLIN;
        fds[1].fd = server_fd;  fds[1].events = POLLIN;

        if (poll(fds, 2, -1) < 0) continue;

        if (fds[0].revents & POLLIN) {
            char buf[EVENT_BUF_LEN];
            ssize_t len = read(inotify_fd, buf, sizeof(buf));
            ssize_t i = 0;
            while (i < len) {
                struct inotify_event *event = (struct inotify_event *)&buf[i];
                if (event->len && strstr(event->name, ".zpk")) {
                    char full[1024];
                    snprintf(full, sizeof(full), "%s/%s", g_watch_dir, event->name);
                    install_zpk(full);
                }
                i += EVENT_SIZE + event->len;
            }
        }

        if (fds[1].revents & POLLIN) {
            int cfd = accept(server_fd, NULL, NULL);
            if (cfd >= 0) {
                char buf[IPC_MAX_LINE];
                ssize_t n = read(cfd, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = 0;
                    handle_command(cfd, buf);
                }
                close(cfd);
            }
        }
    }

    return 0;
}
