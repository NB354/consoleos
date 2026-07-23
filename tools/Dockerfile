# ConsoleOS - environnement de compilation reproductible
#
# Debian 12 (bookworm) : base stable et longuement éprouvée pour Buildroot,
# contrairement à une distribution "rolling release" (Kali, Arch...) dont le
# glibc/GCC trop récents cassent les vieux scripts configure (gnulib) de
# certains outils hôtes historiques (m4, tar...).
FROM debian:12

# Liste EXACTE des paquets obligatoires du manuel officiel Buildroot
# (https://buildroot.org/downloads/manual/manual.html, section
# "2.1. Mandatory packages"), plus les paquets nécessaires aux composants
# spécifiques de ConsoleOS (SDL2, sqlite, libarchive, openssl, libsodium,
# libevdev...) et à genimage (libconfuse, via host-libconfuse construit par
# Buildroot).
RUN apt-get update && apt-get install -y --no-install-recommends \
    which sed make binutils build-essential diffutils gcc g++ bash \
    patch gzip bzip2 perl tar cpio unzip rsync file bc findutils gawk \
    wget ca-certificates \
    git bison flex libncurses-dev libssl-dev pkg-config \
    python3 python3-setuptools \
    cvs mercurial subversion \
    locales-all whiptail sudo \
    && update-ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Buildroot (et certains outils comme GNU tar) refusent explicitement de
# lancer leur configure en root, pour de bonnes raisons de sécurité. On crée
# donc un utilisateur non-root, avec le même UID/GID que l'utilisateur hôte
# (passé en argument de build) afin que les fichiers créés dans le dossier
# monté restent accessibles normalement depuis l'hôte, sans passer par sudo.
ARG USER_ID=1000
ARG GROUP_ID=1000
RUN groupadd -g "${GROUP_ID}" builder \
    && useradd -m -u "${USER_ID}" -g "${GROUP_ID}" -s /bin/bash builder

USER builder
WORKDIR /work
CMD ["/bin/bash"]
