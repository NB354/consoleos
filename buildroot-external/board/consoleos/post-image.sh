#!/bin/sh
# Exécuté par Buildroot après la génération des images (kernel, rootfs)
# pour assembler l'image finale sdcard.img à l'aide de genimage.
set -e

BOARD_DIR="$(dirname "$0")"
BINARIES_DIR="$1"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BINARIES_DIR}/genimage.tmp"

rm -rf "${GENIMAGE_TMP}"

# Depuis une version récente de Buildroot, le paquet rpi-firmware installe
# ses fichiers (bootcode.bin, start*.elf, fixup*.dat, config.txt par
# défaut...) dans un sous-dossier BINARIES_DIR/rpi-firmware/ plutôt qu'à la
# racine de BINARIES_DIR. On les recopie à la racine pour que genimage.cfg
# (qui les référence sans préfixe de dossier) les trouve.
if [ -d "${BINARIES_DIR}/rpi-firmware" ]; then
	find "${BINARIES_DIR}/rpi-firmware" -maxdepth 1 -type f -exec cp -f {} "${BINARIES_DIR}/" \;
	if [ -d "${BINARIES_DIR}/rpi-firmware/overlays" ]; then
		cp -rf "${BINARIES_DIR}/rpi-firmware/overlays" "${BINARIES_DIR}/"
	fi
fi

# genimage attend Image, le dtb, le firmware GPU et config.txt/cmdline.txt
# tous présents dans BINARIES_DIR (Buildroot les y place déjà pour le noyau
# et le dtb ; le firmware RPi vient du paquet rpi-firmware).
# Nos config.txt/cmdline.txt ConsoleOS écrasent volontairement ceux par
# défaut copiés depuis rpi-firmware/ ci-dessus.
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