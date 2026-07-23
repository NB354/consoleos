#!/bin/sh
# Exécuté par Buildroot après la génération des images (kernel, rootfs)
# pour assembler l'image finale sdcard.img à l'aide de genimage.
set -e

BOARD_DIR="$(dirname "$0")"
BINARIES_DIR="$1"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BINARIES_DIR}/genimage.tmp"

rm -rf "${GENIMAGE_TMP}"

# genimage attend Image, le dtb, le firmware GPU et config.txt/cmdline.txt
# tous présents dans BINARIES_DIR (Buildroot les y place déjà pour le noyau
# et le dtb ; le firmware RPi vient du paquet rpi-firmware).
cp "${BOARD_DIR}/config.txt"  "${BINARIES_DIR}/config.txt"
cp "${BOARD_DIR}/cmdline.txt" "${BINARIES_DIR}/cmdline.txt"

genimage \
	--rootpath   "${BINARIES_DIR}/../target" \
	--tmppath    "${GENIMAGE_TMP}" \
	--inputpath  "${BINARIES_DIR}" \
	--outputpath "${BINARIES_DIR}" \
	--config     "${GENIMAGE_CFG}"

echo "Image ConsoleOS générée : ${BINARIES_DIR}/sdcard.img"
exit 0
