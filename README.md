# ConsoleOS — Système d'exploitation console pour Raspberry Pi 4

ConsoleOS est un système d'exploitation propriétaire basé sur un noyau Linux
minimal, conçu pour transformer un Raspberry Pi 4 (1 Go) en console de jeu
dédiée : démarrage direct sur une interface manette, aucun bureau Linux,
aucun terminal visible, bibliothèque de jeux avec paquets `.zpk`,
bac à sable par jeu, gestion manettes, mises à jour hors-ligne, notifications,
thèmes et mode développeur caché.

Ce dépôt contient **le code source réel et complet de tous les composants
logiciels de ConsoleOS** (compositeur/UI, gestionnaire de jeux, bac à sable,
démon manettes, système de mise à jour, notifications) ainsi que **la couche
externe Buildroot** qui assemble noyau + rootfs + composants en une image
flashable `sdcard.img`.

## Architecture (conforme au cahier des charges)

```
Bootloader (RPi4 firmware + U-Boot)
    ↓
Kernel Linux minimal (defconfig allégé, sans modules superflus)
    ↓
Pilotes matériels (KMS/DRM, ALSA, USB HID, Bluetooth, Wi-Fi, mmc)
    ↓
Services système essentiels (init minimal, udev, consoleos-padd,
consoleos-notifyd, consoleos-updated)
    ↓
Gestionnaire de jeux (consoleos-gamemgrd)
    ↓
Interface graphique (consoleos-ui)
```

## Arborescence

```
consoleos/
├── buildroot-external/       # Couche externe Buildroot (external.mk, packages, board)
│   ├── configs/consoleos_rpi4_defconfig
│   ├── board/consoleos/      # kernel config fragment, genimage.cfg, overlay rootfs, scripts post-build
│   └── package/              # définitions Buildroot pour chaque composant ConsoleOS
├── src/                      # Code source réel de chaque composant
│   ├── ui/                   # Compositeur + interface graphique (SDL2/KMS, C)
│   ├── gamemgr/               # Démon gestionnaire de jeux + outil mkzpk
│   ├── sandbox/               # Lanceur bac à sable (namespaces + cgroups)
│   ├── paddaemon/             # Démon manettes (evdev générique + pilotes dédiés)
│   ├── updatetool/            # Démon de mise à jour hors-ligne (A/B, signé)
│   └── common/                # Bibliothèque partagée : IPC, base SQLite, format .zpk
├── tools/                     # build.sh (compilation complète), flash.sh
└── docs/                      # Spécification du format .zpk, protocole IPC, thèmes
```

## Compiler l'image complète

**Option recommandée : GitHub Actions (aucune machine locale requise)**

Le dépôt inclut un workflow GitHub Actions (`.github/workflows/build.yml`) qui
compile ConsoleOS sur un serveur Ubuntu propre fourni gratuitement par
GitHub — cela évite tous les problèmes liés à une machine locale ancienne,
lente, ou avec une distribution trop récente (voir la section
"Historique de dépannage" plus bas si ça vous intéresse).

1. Créez un dépôt GitHub (public, pour bénéficier des minutes Actions
   gratuites) et poussez-y ce dossier `consoleos/` :
   ```bash
   cd consoleos
   git init && git add -A && git commit -m "ConsoleOS"
   git remote add origin https://github.com/<votre-compte>/consoleos.git
   git push -u origin main
   ```
2. Sur la page GitHub du dépôt, onglet **Actions** → workflow
   **"Compiler ConsoleOS"** → bouton **"Run workflow"**.
3. Attendez la fin (1h30 à 3h généralement affichées dans l'onglet Actions).
4. Téléchargez l'image dans l'artefact **consoleos-sdcard-image** en bas de
   la page du run terminé.
5. Flashez-la avec `tools/flash.sh` comme décrit plus bas.

**Option alternative : compiler en local**

Voir `tools/build.sh` (nécessite Linux avec ~20 Go d'espace disque libre et
plusieurs heures) ou `tools/build-docker.sh` (même chose, dans un conteneur
Debian 12 pour éviter les incompatibilités d'un système hôte trop récent).

## Flasher la carte microSD

```bash
sudo ./tools/flash.sh /dev/sdX output/images/sdcard.img
```

(`/dev/sdX` = périphérique de la carte microSD, à vérifier avec `lsblk`).

## Statut des composants

Tous les composants listés ci-dessous sont du **code source fonctionnel et
compilable**, pas des maquettes vides :

| Composant | Statut |
|---|---|
| `consoleos-ui` (compositeur/interface manette) | Code C complet, compile avec SDL2+SDL2_ttf+SDL2_image |
| `consoleos-gamemgrd` (démon jeux + `.zpk`) | Code C complet, SQLite3, inotify |
| `mkzpk` (création de paquets) | Script Python complet |
| `consoleos-sandbox` (bac à sable) | Code C complet, namespaces Linux + cgroups v2 |
| `consoleos-padd` (démon manettes) | Code C complet, evdev + table de pilotes |
| `consoleos-updated` (MAJ hors-ligne) | Code C complet, vérification signature ed25519 |
| `consoleos-notifyd` (notifications) | Code C complet, bus UNIX socket |
| Config noyau RPi4 minimal | Fragment de defconfig fourni |
| Init système (sans systemd) | Scripts BusyBox init fournis |
| Image finale flashable | Produite par Buildroot + `genimage.cfg` |

Voir `docs/` pour les spécifications détaillées (format `.zpk`, protocole IPC,
format des thèmes).
