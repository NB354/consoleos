/* ConsoleOS - protocole IPC interne
 *
 * Tous les démons ConsoleOS communiquent via des sockets UNIX (SOCK_STREAM)
 * en échangeant des lignes JSON terminées par '\n'. C'est volontairement
 * simple (pas de dépendance DBus) pour rester léger sur 1 Go de RAM.
 *
 * Sockets standard :
 *   /run/consoleos/notify.sock   -> consoleos-notifyd
 *   /run/consoleos/pad.sock      -> consoleos-padd
 *   /run/consoleos/gamemgr.sock  -> consoleos-gamemgrd
 *   /run/consoleos/update.sock   -> consoleos-updated
 */
#ifndef CONSOLEOS_IPC_H
#define CONSOLEOS_IPC_H

#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>

#define CONSOLEOS_RUN_DIR        "/run/consoleos"
#define CONSOLEOS_NOTIFY_SOCK    CONSOLEOS_RUN_DIR "/notify.sock"
#define CONSOLEOS_PAD_SOCK       CONSOLEOS_RUN_DIR "/pad.sock"
#define CONSOLEOS_GAMEMGR_SOCK   CONSOLEOS_RUN_DIR "/gamemgr.sock"
#define CONSOLEOS_UPDATE_SOCK    CONSOLEOS_RUN_DIR "/update.sock"

#define IPC_MAX_LINE 4096

/* Crée et lie un socket UNIX serveur (stream), non bloquant sur accept(). */
static inline int ipc_server_socket(const char *path) {
    struct sockaddr_un addr;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    unlink(path);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 16) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Ouvre une connexion client vers un socket ConsoleOS. */
static inline int ipc_client_connect(const char *path) {
    struct sockaddr_un addr;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Envoie une ligne JSON (ajoute le '\n' terminal). */
static inline int ipc_send_line(int fd, const char *json) {
    size_t len = strlen(json);
    ssize_t w = write(fd, json, len);
    if (w < 0) return -1;
    if (write(fd, "\n", 1) < 0) return -1;
    return 0;
}

#endif /* CONSOLEOS_IPC_H */
