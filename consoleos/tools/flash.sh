#!/bin/bash
# ConsoleOS - flash.sh
#
# Écrit l'image ConsoleOS sur une carte microSD.
#
# Usage : sudo ./flash.sh /dev/sdX chemin/vers/sdcard.img
#
# ATTENTION : /dev/sdX sera intégralement effacé. Vérifiez le périphérique
# avec `lsblk` avant de lancer ce script — une erreur ici peut effacer un
# disque de votre ordinateur.
set -euo pipefail

DEVICE="${1:-}"
IMAGE="${2:-}"

if [ -z "${DEVICE}" ] || [ -z "${IMAGE}" ]; then
    echo "Usage: sudo $0 /dev/sdX chemin/vers/sdcard.img" >&2
    exit 1
fi

if [ ! -b "${DEVICE}" ]; then
    echo "Erreur : ${DEVICE} n'est pas un périphérique bloc." >&2
    exit 1
fi

if [ ! -f "${IMAGE}" ]; then
    echo "Erreur : image introuvable : ${IMAGE}" >&2
    exit 1
fi

echo "=== ConsoleOS flash ==="
echo "Périphérique cible : ${DEVICE}"
lsblk "${DEVICE}"
echo ""
read -rp "Ce périphérique sera ENTIÈREMENT EFFACÉ. Continuer ? [oui/NON] " CONFIRM
if [ "${CONFIRM}" != "oui" ]; then
    echo "Annulé."
    exit 0
fi

echo "--- Démontage des partitions existantes ---"
for part in "${DEVICE}"*; do
    umount "${part}" 2>/dev/null || true
done

echo "--- Écriture de l'image (peut prendre plusieurs minutes) ---"
dd if="${IMAGE}" of="${DEVICE}" bs=4M status=progress conv=fsync

sync
echo ""
echo "=== Terminé. Carte microSD ConsoleOS prête. ==="
