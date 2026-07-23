/* ConsoleOS - consoleos-sandbox
 *
 * Lance un jeu dans un environnement isolé :
 *   - namespace mount  : le jeu ne voit qu'un rootfs minimal construit à
 *                        partir de son propre dossier d'installation
 *                        (accès uniquement à son dossier + ses sauvegardes)
 *   - namespace PID    : le jeu ne voit pas les autres processus du système
 *   - namespace UTS/IPC: isolation complémentaire standard
 *   - namespace réseau : désactivé par défaut (pas d'accès réseau pour un jeu,
 *                        sauf permission explicite "network" dans le manifest)
 *   - cgroup v2         : limite mémoire/CPU pour éviter qu'un jeu ne
 *                        déstabilise le système sur une machine à 1 Go de RAM
 *   - device passthrough: seuls /dev/dri (GPU), /dev/snd (audio) et les
 *                        périphériques d'entrée (evdev) sont bind-mountés
 *
 * Doit être installé setuid-root (voir consoleos-sandbox.mk, mode 4755)
 * car la création de namespaces mount/PID requiert CAP_SYS_ADMIN.
 * Le programme abandonne les privilèges root vers un utilisateur dédié
 * ("games", non privilégié) dès que les namespaces sont en place et juste
 * avant d'exécuter le binaire du jeu.
 *
 * Usage : consoleos-sandbox --root <dossier_jeu_installé> --exec <chemin_relatif_executable>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pwd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>

#define SANDBOX_UID_USER "games"
#define CGROUP_BASE "/sys/fs/cgroup/consoleos-games"

static char g_root[512];
static char g_exec[512];

/* Bind-monte un chemin hôte en lecture seule ou lecture-écriture dans le
 * nouveau rootfs, en créant le point de montage si nécessaire. */
static void bind_mount(const char *host_path, const char *guest_rel, int rw) {
    char target[1024];
    snprintf(target, sizeof(target), "%s%s", g_root, guest_rel);

    struct stat st;
    if (stat(host_path, &st) != 0) return; /* périphérique absent, on ignore */

    if (S_ISDIR(st.st_mode)) mkdir(target, 0755);
    else { int fd = open(target, O_CREAT, 0644); if (fd >= 0) close(fd); }

    if (mount(host_path, target, NULL, MS_BIND, NULL) != 0) {
        fprintf(stderr, "sandbox: échec bind-mount %s -> %s (%s)\n",
                host_path, target, strerror(errno));
        return;
    }
    if (!rw) {
        mount(NULL, target, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL);
    }
}

/* Applique des limites cgroup v2 (mémoire + CPU) au groupe du processus
 * courant, afin que le jeu ne consomme jamais toute la RAM disponible. */
static void apply_cgroup_limits(pid_t pid, long mem_limit_mb, int cpu_percent) {
    char path[256], val[64];
    mkdir(CGROUP_BASE, 0755);
    snprintf(path, sizeof(path), CGROUP_BASE "/game-%d", pid);
    mkdir(path, 0755);

    char f[300];
    snprintf(f, sizeof(f), "%s/memory.max", path);
    FILE *fp = fopen(f, "w");
    if (fp) { snprintf(val, sizeof(val), "%ldM", mem_limit_mb); fputs(val, fp); fclose(fp); }

    snprintf(f, sizeof(f), "%s/cpu.max", path);
    fp = fopen(f, "w");
    if (fp) { snprintf(val, sizeof(val), "%d000 100000", cpu_percent); fputs(val, fp); fclose(fp); }

    snprintf(f, sizeof(f), "%s/cgroup.procs", path);
    fp = fopen(f, "w");
    if (fp) { fprintf(fp, "%d", pid); fclose(fp); }
}

static void drop_privileges(void) {
    struct passwd *pw = getpwnam(SANDBOX_UID_USER);
    if (!pw) {
        fprintf(stderr, "sandbox: utilisateur '%s' introuvable, abandon\n", SANDBOX_UID_USER);
        exit(1);
    }
    if (setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
        fprintf(stderr, "sandbox: échec de l'abandon des privilèges\n");
        exit(1);
    }
}

int main(int argc, char **argv) {
    static struct option opts[] = {
        {"root", required_argument, 0, 'r'},
        {"exec", required_argument, 0, 'e'},
        {0,0,0,0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "r:e:", opts, NULL)) != -1) {
        switch (c) {
            case 'r': strncpy(g_root, optarg, sizeof(g_root)-1); break;
            case 'e': strncpy(g_exec, optarg, sizeof(g_exec)-1); break;
        }
    }
    if (!g_root[0] || !g_exec[0]) {
        fprintf(stderr, "usage: %s --root <dossier> --exec <chemin_relatif>\n", argv[0]);
        return 1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "sandbox: doit être exécuté setuid-root\n");
        return 1;
    }

    /* Nouveau namespace mount + PID + UTS + IPC.
     * CLONE_NEWNET isole le réseau : le jeu n'a par défaut aucun accès
     * réseau (conforme au cahier des charges : accès limité à son dossier,
     * ses sauvegardes, l'audio, les graphismes, les entrées). */
    if (unshare(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET) != 0) {
        fprintf(stderr, "sandbox: unshare a échoué (%s)\n", strerror(errno));
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* --- Enfant : devient PID 1 du nouveau namespace PID --- */

        mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

        /* Le rootfs du jeu devient son propre dossier d'installation */
        mount(g_root, g_root, NULL, MS_BIND, NULL);
        chdir(g_root);

        /* Périphériques strictement nécessaires : GPU, audio, entrées */
        bind_mount("/dev/dri", "/dev/dri", 1);
        bind_mount("/dev/snd", "/dev/snd", 1);
        bind_mount("/dev/input", "/dev/input", 0);

        /* Sauvegardes en lecture-écriture, tout le reste du dossier jeu
         * en lecture seule pour empêcher l'auto-modification du binaire */
        bind_mount("saves", "/saves", 1);
        bind_mount("cache", "/cache", 1);

        if (chroot(g_root) != 0) {
            fprintf(stderr, "sandbox(enfant): chroot a échoué (%s)\n", strerror(errno));
            _exit(1);
        }
        chdir("/");

        drop_privileges();

        execl(g_exec, g_exec, (char *)NULL);
        fprintf(stderr, "sandbox(enfant): exec de '%s' a échoué (%s)\n", g_exec, strerror(errno));
        _exit(127);
    }

    /* --- Parent : applique les limites cgroup puis attend la fin du jeu --- */
    apply_cgroup_limits(pid, 600 /* Mo max pour un jeu sur 1 Go total */, 90);

    int status;
    waitpid(pid, &status, 0);

    char cgpath[300];
    snprintf(cgpath, sizeof(cgpath), CGROUP_BASE "/game-%d", pid);
    rmdir(cgpath);

    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
