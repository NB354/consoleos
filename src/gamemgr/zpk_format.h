/* ConsoleOS - format de paquet de jeu .zpk
 *
 * Un fichier .zpk est une archive tar compressée (zstd ou gzip, lue via
 * libarchive) contenant obligatoirement, à la racine :
 *
 *   manifest.json     - métadonnées (voir struct zpk_manifest ci-dessous)
 *   icon.png          - icône carrée (256x256 recommandé)
 *   cover.png         - jaquette (600x800 recommandé)
 *   bin/<exe>         - exécutable ARM64 recompilé pour ConsoleOS
 *   data/             - ressources du jeu
 *   config/           - configuration par défaut
 *   saves/            - dossier vide, réservé aux sauvegardes utilisateur
 *   cache/            - dossier vide, réservé au cache runtime
 *   sig.bin           - signature ed25519 du paquet (voir docs/zpk-format.md)
 *
 * manifest.json (exemple) :
 * {
 *   "id": "com.studio.jeu",
 *   "name": "Nom du jeu",
 *   "version": "1.2.0",
 *   "executable": "bin/jeu",
 *   "min_ram_mb": 256,
 *   "permissions": ["audio", "video", "input"],
 *   "publisher": "Studio",
 *   "checksum_sha256": "…"
 * }
 *
 * Le système ne monte/exécute JAMAIS directement une ROM : seul un .zpk
 * valide, vérifié et indexé peut être lancé, et toujours via
 * consoleos-sandbox (bac à sable).
 */
#ifndef CONSOLEOS_ZPK_FORMAT_H
#define CONSOLEOS_ZPK_FORMAT_H

#define ZPK_MAX_ID_LEN       128
#define ZPK_MAX_NAME_LEN     256
#define ZPK_MAX_VERSION_LEN  32
#define ZPK_MAX_PATH_LEN     512
#define ZPK_MAX_PERMS        8

typedef struct {
    char id[ZPK_MAX_ID_LEN];
    char name[ZPK_MAX_NAME_LEN];
    char version[ZPK_MAX_VERSION_LEN];
    char executable[ZPK_MAX_PATH_LEN];
    int  min_ram_mb;
    char permissions[ZPK_MAX_PERMS][32];
    int  n_permissions;
    char publisher[ZPK_MAX_NAME_LEN];
    char checksum_sha256[65];
} zpk_manifest_t;

#endif /* CONSOLEOS_ZPK_FORMAT_H */
