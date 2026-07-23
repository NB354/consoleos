#!/bin/bash
# ConsoleOS - build-docker.sh
#
# Compile ConsoleOS dans un conteneur Docker Debian 12, pour éviter les
# incompatibilités entre les vieux scripts configure (gnulib) des outils
# hôtes de Buildroot et un système hôte trop récent (distribution rolling
# release comme Kali/Arch, GCC 15+, glibc très récente...).
#
# Prérequis : Docker installé sur la machine hôte (sudo apt install docker.io
# puis sudo usermod -aG docker $USER, se reconnecter).
#
# Usage :
#   ./build-docker.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
IMAGE_NAME="consoleos-builder"

echo "=== ConsoleOS build (Docker, Debian 12) ==="

echo "--- Construction de l'image Docker (une seule fois, ~1-2 min) ---"
docker build \
    --build-arg USER_ID="$(id -u)" \
    --build-arg GROUP_ID="$(id -g)" \
    -t "${IMAGE_NAME}" -f "${SCRIPT_DIR}/Dockerfile" "${SCRIPT_DIR}"

echo "--- Lancement de la compilation dans le conteneur ---"
echo "    Le dossier ${PROJECT_ROOT} est monté dans le conteneur : rien n'est"
echo "    perdu si vous arrêtez le conteneur, tout reste sur votre disque."
docker run --rm -it \
    -v "${PROJECT_ROOT}:/work/consoleos" \
    -w /work/consoleos/tools \
    "${IMAGE_NAME}" \
    ./build.sh

echo ""
echo "=== Terminé. Image dans consoleos/buildroot/output/images/sdcard.img ==="
