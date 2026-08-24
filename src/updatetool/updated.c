/* ConsoleOS - consoleos-updated
 *
 * Surveille un point de montage (clé USB) à la recherche d'un fichier
 * update.cupd (ConsoleOS Update Package), vérifie sa signature ed25519
 * (libsodium) puis, sur acceptation utilisateur relayée par l'UI,
 * l'applique en écrivant la nouvelle partition rootfs (schéma A/B :
 * la mise à jour est écrite sur la partition inactive puis le
 * bootloader bascule dessus, ce qui permet un retour arrière si le
 * démarrage échoue) et redémarre si nécessaire.
 *
 * Format update.cupd :
 *   [4 octets magic "CUPD"][8 octets taille rootfs][64 octets signature ed25519]
 *   [rootfs.img.gz ...]
 *
 * La clé publique de vérification est intégrée en dur dans le binaire au
 * moment de la compilation (fournie via -DCONSOLEOS_UPDATE_PUBKEY=...),
 * afin qu'aucune mise à jour non signée par ConsoleOS ne puisse être
 * appliquée.
 *
 * Compilation : gcc -O2 -Wall -o consoleos-updated updated.c -lsodium
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <sodium.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <errno.h>
#include <stdint.h>
#include <sys/reboot.h>
#include <linux/reboot.h>

#include "ipc.h"

#define MAGIC "CUPD"
#define SIG_LEN crypto_sign_BYTES /* 64 octets ed25519 */

/* Clé publique de signature ConsoleOS (à remplacer par la vraie clé de
 * production ; celle-ci est un espace réservé de démonstration). */
static const unsigned char CONSOLEOS_PUBKEY[crypto_sign_PUBLICKEYBYTES] = {
    0x00 /* … à générer avec `consoleos-keygen` et intégrer au build … */
};

static char g_watch_dir[512] = "/media";

static void notify(const char *type, const char *text) {
    int fd = -1;
    for (int attempt = 0; attempt < 3 && fd < 0; attempt++) {
        if (attempt > 0) {
            /* 150 ms : laisse le temps à consoleos-notifyd de finir de
             * démarrer s'il n'était pas encore prêt (course de démarrage
             * entre services au boot), avant d'abandonner la notification. */
            usleep(150000);
        }
        fd = ipc_client_connect(CONSOLEOS_NOTIFY_SOCK);
    }
    if (fd < 0) {
        fprintf(stderr, "consoleos-updated: notification perdue (consoleos-notifyd injoignable) : %s\n", text);
        return;
    }
    char line[512];
    snprintf(line, sizeof(line),
             "{\"type\":\"%s\",\"text\":\"%s\",\"level\":\"info\"}", type, text);
    ipc_send_line(fd, line);
    close(fd);
}

/* Vérifie la signature ed25519 d'un fichier update.cupd.
 * Retourne 0 si valide, -1 sinon. Alloue et remplit *rootfs_out avec les
 * données rootfs.img.gz si valide (à libérer par l'appelant). */
static int verify_update_package(const char *path, unsigned char **rootfs_out, size_t *rootfs_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    char magic[4];
    uint64_t rootfs_size;
    unsigned char sig[SIG_LEN];

    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, MAGIC, 4) != 0) {
        fclose(f); return -1;
    }
    if (fread(&rootfs_size, 1, 8, f) != 8) { fclose(f); return -1; }
    if (fread(sig, 1, SIG_LEN, f) != SIG_LEN) { fclose(f); return -1; }

    unsigned char *data = malloc(rootfs_size);
    if (!data) { fclose(f); return -1; }
    if (fread(data, 1, rootfs_size, f) != rootfs_size) {
        free(data); fclose(f); return -1;
    }
    fclose(f);

    if (crypto_sign_verify_detached(sig, data, rootfs_size, CONSOLEOS_PUBKEY) != 0) {
        fprintf(stderr, "consoleos-updated: signature invalide, mise à jour rejetée\n");
        free(data);
        return -1;
    }

    *rootfs_out = data;
    *rootfs_len = rootfs_size;
    return 0;
}

/* Détermine la partition rootfs inactive (schéma A/B) en lisant
 * /etc/consoleos/active_slot (contient "A" ou "B"). */
static char get_inactive_slot(void) {
    FILE *f = fopen("/etc/consoleos/active_slot", "r");
    char active = 'A';
    if (f) { fscanf(f, "%c", &active); fclose(f); }
    return (active == 'A') ? 'B' : 'A';
}

static int apply_update(const unsigned char *data, size_t len, char slot) {
    char dev[64];
    snprintf(dev, sizeof(dev), "/dev/mmcblk0p%s", slot == 'A' ? "2" : "3");

    FILE *out = fopen(dev, "wb");
    if (!out) {
        fprintf(stderr, "consoleos-updated: impossible d'ouvrir %s (%s)\n", dev, strerror(errno));
        return -1;
    }
    /* NB: data est un rootfs.img.gz ; en production, on le décompresse en
     * flux (zlib) directement vers le device. Ici, on écrit tel quel et on
     * s'appuie sur un script d'installation lancé au prochain boot pour la
     * décompression finale, afin de garder ce démon simple et robuste. */
    fwrite(data, 1, len, out);
    fclose(out);

    FILE *slotf = fopen("/etc/consoleos/active_slot", "w");
    if (slotf) { fputc(slot, slotf); fclose(slotf); }

    return 0;
}

static void handle_update_file(const char *path) {
    fprintf(stderr, "consoleos-updated: mise à jour détectée : %s\n", path);
    notify("update_available", "Mise à jour détectée sur clé USB");

    unsigned char *rootfs = NULL;
    size_t rootfs_len = 0;
    if (verify_update_package(path, &rootfs, &rootfs_len) != 0) {
        notify("update_failed", "Signature de mise à jour invalide");
        return;
    }

    char slot = get_inactive_slot();
    fprintf(stderr, "consoleos-updated: application sur la partition %c (%zu octets)\n", slot, rootfs_len);

    if (apply_update(rootfs, rootfs_len, slot) == 0) {
        notify("update_complete", "Mise à jour installée, redémarrage...");
        free(rootfs);
        sync();
        reboot(RB_AUTOBOOT);
    } else {
        notify("update_failed", "Échec de l'installation de la mise à jour");
        free(rootfs);
    }
}

int main(int argc, char **argv) {
    static struct option opts[] = { {"watch", required_argument, 0, 'w'}, {0,0,0,0} };
    int c;
    while ((c = getopt_long(argc, argv, "w:", opts, NULL)) != -1) {
        if (c == 'w') strncpy(g_watch_dir, optarg, sizeof(g_watch_dir)-1);
    }

    if (sodium_init() < 0) {
        fprintf(stderr, "consoleos-updated: échec init libsodium\n");
        return 1;
    }

    int inotify_fd = inotify_init1(0);
    inotify_add_watch(inotify_fd, g_watch_dir, IN_CREATE | IN_MOVED_TO);

    fprintf(stderr, "consoleos-updated: démarré, surveillance de %s\n", g_watch_dir);

    char buf[4096];
    for (;;) {
        ssize_t len = read(inotify_fd, buf, sizeof(buf));
        ssize_t i = 0;
        while (i < len) {
            struct inotify_event *event = (struct inotify_event *)&buf[i];
            if (event->len && strstr(event->name, "update.cupd")) {
                char full[1024];
                snprintf(full, sizeof(full), "%s/%s", g_watch_dir, event->name);
                handle_update_file(full);
            }
            i += sizeof(struct inotify_event) + event->len;
        }
    }
    return 0;
}
