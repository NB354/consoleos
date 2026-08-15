/* ConsoleOS - consoleos-notifyd
 *
 * Bus de notifications minimal : les producteurs (gamemgr, updated, padd...)
 * envoient une ligne JSON sur /run/consoleos/notify.sock ; le démon la
 * diffuse (fan-out) à tous les abonnés actuellement connectés (typiquement
 * consoleos-ui, qui affiche une pastille discrète à l'écran).
 *
 * Message attendu en entrée :
 *   {"type":"update_available","text":"Mise à jour disponible","level":"info"}
 *
 * Le démon ne fait aucune validation lourde : il relaie tel quel. La mise
 * en forme visuelle est de la responsabilité de consoleos-ui.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "ipc.h"

#define MAX_CLIENTS 32

typedef struct {
    int fd;
    int subscriber; /* 1 si le client a envoyé SUBSCRIBE */
} client_t;

static client_t clients[MAX_CLIENTS];

static void broadcast(const char *line, int exclude_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd > 0 && clients[i].subscriber && clients[i].fd != exclude_fd) {
            ipc_send_line(clients[i].fd, line);
        }
    }
}

static int add_client(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == 0) {
            clients[i].fd = fd;
            clients[i].subscriber = 0;
            return i;
        }
    }
    return -1;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    mkdir(CONSOLEOS_RUN_DIR, 0755);

    int server_fd = ipc_server_socket(CONSOLEOS_NOTIFY_SOCK);
    if (server_fd < 0) {
        fprintf(stderr, "consoleos-notifyd: impossible de créer le socket\n");
        return 1;
    }

    memset(clients, 0, sizeof(clients));
    fprintf(stderr, "consoleos-notifyd: démarré sur %s\n", CONSOLEOS_NOTIFY_SOCK);

    for (;;) {
        struct pollfd fds[MAX_CLIENTS + 1];
        int nfds = 0;
        fds[nfds].fd = server_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0) {
                fds[nfds].fd = clients[i].fd;
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }

        int ready = poll(fds, nfds, -1);
        if (ready < 0) continue;

        if (fds[0].revents & POLLIN) {
            int cfd = accept(server_fd, NULL, NULL);
            if (cfd >= 0) {
                if (add_client(cfd) < 0) close(cfd);
            }
        }

        for (int i = 1; i < nfds; i++) {
            if (!(fds[i].revents & POLLIN)) continue;
            int fd = fds[i].fd;
            char buf[IPC_MAX_LINE];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            if (n <= 0) {
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (clients[j].fd == fd) { clients[j].fd = 0; }
                }
                close(fd);
                continue;
            }
            buf[n] = 0;

            /* Ligne "SUBSCRIBE" -> ce client devient abonné aux diffusions */
            if (strncmp(buf, "SUBSCRIBE", 9) == 0) {
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (clients[j].fd == fd) clients[j].subscriber = 1;
                }
                continue;
            }

            /* Sinon : c'est une notification à diffuser à tous les abonnés */
            broadcast(buf, fd);
        }
    }

    return 0;
}
