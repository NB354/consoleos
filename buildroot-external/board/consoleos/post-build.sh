#!/bin/sh
# Exécuté par Buildroot après la construction du rootfs, avant la génération
# de l'image finale.
set -e

BOARD_DIR="$(dirname "$0")"
TARGET_DIR="$1"

# Copie config.txt / cmdline.txt à la racine du rootfs pour que
# post-image.sh puisse les inclure dans la partition boot.vfat
mkdir -p "${TARGET_DIR}/boot"
cp "${BOARD_DIR}/config.txt"   "${TARGET_DIR}/boot/config.txt"
cp "${BOARD_DIR}/cmdline.txt"  "${TARGET_DIR}/boot/cmdline.txt"

# S'assure que le mode développeur est désactivé par défaut
mkdir -p "${TARGET_DIR}/etc/consoleos"
echo "devmode_enabled=0" > "${TARGET_DIR}/etc/consoleos/devmode.conf"

# Droits d'exécution sur les scripts d'init ConsoleOS
chmod +x "${TARGET_DIR}/etc/init.d/"S* 2>/dev/null || true

exit 0