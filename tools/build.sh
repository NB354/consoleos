#!/bin/bash
# ConsoleOS - build.sh
#
# Compile l'image système complète (noyau + rootfs + tous les composants
# ConsoleOS) via Buildroot, puis produit sdcard.img flashable.
#
# Prérequis (machine Linux x86_64, PAS dans un conteneur de chat) :
#   - ~20 Go d'espace disque libre
#   - build-essential, git, bc, bison, flex, libssl-dev, rsync, unzip,
#     wget, cpio, python3, libncurses-dev
#   - une connexion internet (téléchargement de Buildroot + sources noyau)
#   - 1 à 3 heures selon la machine
#
# Usage :
#   ./build.sh
#
# CONSEIL : lancez ce script dans une session persistante (tmux, screen, ou
# `nohup ./build.sh > build.log 2>&1 &`) : sur une machine lente, la
# compilation prend facilement plusieurs heures, et un terminal fermé par
# erreur (déconnexion SSH, fermeture de fenêtre) tuerait le processus en
# cours. Si le script s'arrête, le relancer reprend là où il en était
# (Buildroot ne recompile pas ce qui est déjà fait).
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
BUILDROOT_VERSION="2026.02.x"
# NB: on utilise la branche LTS la plus récente plutôt qu'une version figée
# car les LTS Buildroot antérieures à 2025.02 ne compilent pas leurs outils
# hôtes (m4, bash...) avec GCC 15+, présent sur les distributions Linux
# récentes (GCC 15 a changé son standard C par défaut de gnu17 à gnu23,
# ce qui casse d'anciennes copies de gnulib). Voir CHANGES de Buildroot,
# section "Various fix for GCC15 host Build issues".
BUILDROOT_DIR="${PROJECT_ROOT}/buildroot"

echo "=== ConsoleOS build ==="
echo "Répertoire projet : ${PROJECT_ROOT}"

if [ ! -d "${BUILDROOT_DIR}" ]; then
    echo "--- Téléchargement de Buildroot (${BUILDROOT_VERSION}) ---"
    git clone --branch "${BUILDROOT_VERSION}" --depth 1 \
        https://github.com/buildroot/buildroot.git "${BUILDROOT_DIR}"
fi

cd "${BUILDROOT_DIR}"

echo "--- Application de la configuration ConsoleOS ---"
make BR2_EXTERNAL="${PROJECT_ROOT}/buildroot-external" consoleos_rpi4_defconfig

# Limite le nombre de compilations parallèles en fonction de la RAM disponible
# (environ 1 job par 2 Go de RAM totale). Sur une machine à faible RAM, compiler
# glibc/GCC avec `make -j$(nproc)` peut lancer plusieurs cc1/cc1plus gourmands
# en mémoire simultanément et faire échouer la compilation de façon silencieuse
# (message générique "cannot compile" au lieu d'une vraie erreur mémoire).
TOTAL_RAM_KB=$(awk '/MemTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo 4000000)
TOTAL_RAM_GB=$(( TOTAL_RAM_KB / 1024 / 1024 ))
SAFE_JOBS=$(( TOTAL_RAM_GB / 2 ))
[ "${SAFE_JOBS}" -lt 1 ] && SAFE_JOBS=1
NPROC=$(nproc)
[ "${SAFE_JOBS}" -gt "${NPROC}" ] && SAFE_JOBS="${NPROC}"
JOBS="${BR2_JLEVEL:-${SAFE_JOBS}}"

echo "--- Compilation (noyau, toolchain, rootfs, composants ConsoleOS) ---"
echo "    RAM détectée : ${TOTAL_RAM_GB} Go -> ${JOBS} job(s) de compilation en parallèle"
echo "    (forcer un autre nombre : BR2_JLEVEL=N ./build.sh)"
echo "    Cette étape prend 1 à 3 heures et nécessite ~20 Go d'espace disque."
make BR2_EXTERNAL="${PROJECT_ROOT}/buildroot-external" -j"${JOBS}"

IMAGE="${BUILDROOT_DIR}/output/images/sdcard.img"
if [ -f "${IMAGE}" ]; then
    echo ""
    echo "=== Image ConsoleOS générée avec succès ==="
    echo "    ${IMAGE}"
    echo ""
    echo "Pour flasher : sudo ${SCRIPT_DIR}/flash.sh /dev/sdX ${IMAGE}"
else
    echo "Erreur : sdcard.img non trouvée, vérifiez les logs de compilation ci-dessus." >&2
    exit 1
fi
