#!/usr/bin/env python3
"""
mkzpk - outil de création de paquets de jeu ConsoleOS (.zpk)

Usage :
    mkzpk create --src ./mon_jeu/ --out MonJeu.zpk \
                  --id com.studio.monjeu --name "Mon Jeu" --version 1.0.0 \
                  --exec bin/monjeu --publisher "Studio" \
                  [--min-ram 256] [--key signing_key.pem]

Structure attendue dans --src :
    bin/<executable>       (recompilé ARM64 pour ConsoleOS)
    data/                  (ressources)
    config/                (config par défaut, optionnel)
    icon.png
    cover.png

L'outil génère :
    manifest.json  (métadonnées + checksum SHA-256 de l'exécutable)
    sig.bin        (signature ed25519 du manifest, si --key fourni)
et assemble le tout dans une archive tar.gz nommée --out.
"""
import argparse
import hashlib
import json
import os
import sys
import tarfile
import tempfile
import time

try:
    from nacl.signing import SigningKey
    HAVE_NACL = True
except ImportError:
    HAVE_NACL = False


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def build_manifest(args, exe_path):
    return {
        "id": args.id,
        "name": args.name,
        "version": args.version,
        "executable": args.exec,
        "min_ram_mb": args.min_ram,
        "permissions": args.permissions.split(",") if args.permissions else ["audio", "video", "input"],
        "publisher": args.publisher,
        "checksum_sha256": sha256_of(exe_path),
        "built_at": int(time.time()),
    }


def cmd_create(args):
    exe_path = os.path.join(args.src, args.exec)
    if not os.path.isfile(exe_path):
        sys.exit(f"Erreur : exécutable introuvable : {exe_path}")

    manifest = build_manifest(args, exe_path)

    with tempfile.TemporaryDirectory() as tmp:
        manifest_path = os.path.join(tmp, "manifest.json")
        with open(manifest_path, "w") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)

        sig_path = os.path.join(tmp, "sig.bin")
        if args.key:
            if not HAVE_NACL:
                sys.exit("Erreur : PyNaCl requis pour signer (pip install pynacl)")
            with open(args.key, "rb") as f:
                key_bytes = f.read()
            signing_key = SigningKey(key_bytes)
            with open(manifest_path, "rb") as f:
                signed = signing_key.sign(f.read())
            with open(sig_path, "wb") as f:
                f.write(signed.signature)
        else:
            open(sig_path, "wb").close()  # non signé (dev uniquement)

        out_path = args.out
        with tarfile.open(out_path, "w:gz") as tar:
            tar.add(manifest_path, arcname="manifest.json")
            tar.add(sig_path, arcname="sig.bin")
            for entry in ("bin", "data", "config", "icon.png", "cover.png"):
                full = os.path.join(args.src, entry)
                if os.path.exists(full):
                    tar.add(full, arcname=entry)
            # dossiers réservés, toujours créés vides
            for reserved in ("saves", "cache"):
                info = tarfile.TarInfo(name=reserved)
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                tar.addfile(info)

        print(f"Paquet créé : {out_path}")
        print(f"  id={manifest['id']}  version={manifest['version']}")
        print(f"  checksum sha256={manifest['checksum_sha256']}")
        if not args.key:
            print("  ATTENTION : paquet non signé (usage développement uniquement)")


def main():
    parser = argparse.ArgumentParser(description="Outil de création de paquets .zpk ConsoleOS")
    sub = parser.add_subparsers(dest="command", required=True)

    p_create = sub.add_parser("create", help="Créer un paquet .zpk")
    p_create.add_argument("--src", required=True, help="Dossier source du jeu")
    p_create.add_argument("--out", required=True, help="Fichier .zpk de sortie")
    p_create.add_argument("--id", required=True)
    p_create.add_argument("--name", required=True)
    p_create.add_argument("--version", required=True)
    p_create.add_argument("--exec", required=True, dest="exec", help="Chemin relatif de l'exécutable")
    p_create.add_argument("--publisher", default="Inconnu")
    p_create.add_argument("--min-ram", type=int, default=128, dest="min_ram")
    p_create.add_argument("--permissions", default="")
    p_create.add_argument("--key", default=None, help="Clé privée ed25519 pour signer le paquet")
    p_create.set_defaults(func=cmd_create)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
