# Format de paquet ConsoleOS (.zpk)

## Vue d'ensemble

Un `.zpk` est une archive `tar.gz` avec une structure fixe. Le système ne
monte et n'exécute **jamais** une ROM directement : seul un `.zpk` valide,
vérifié par `consoleos-gamemgrd`, peut être installé puis lancé, et
toujours via `consoleos-sandbox`.

## Structure

```
monjeu.zpk
├── manifest.json      (métadonnées, obligatoire)
├── sig.bin            (signature ed25519 du manifest, obligatoire en prod)
├── icon.png            (256x256 recommandé)
├── cover.png            (600x800 recommandé)
├── bin/
│   └── <executable>    (ARM64, recompilé pour ConsoleOS)
├── data/                (ressources du jeu)
├── config/              (configuration par défaut)
├── saves/               (vide à la création, rempli à l'usage)
└── cache/               (vide à la création, cache runtime)
```

## manifest.json

| Champ | Type | Description |
|---|---|---|
| `id` | string | Identifiant unique inversé (`com.studio.jeu`) |
| `name` | string | Nom affiché |
| `version` | string | Version sémantique |
| `executable` | string | Chemin relatif vers le binaire (ex. `bin/jeu`) |
| `min_ram_mb` | int | RAM minimale requise |
| `permissions` | array | Sous-ensemble de `["audio","video","input","network"]` |
| `publisher` | string | Éditeur/développeur |
| `checksum_sha256` | string | SHA-256 de l'exécutable |

## Cycle de vie (géré par consoleos-gamemgrd)

1. **Détection automatique** — `consoleos-gamemgrd` surveille les points de
   montage USB via `inotify` et repère tout fichier `*.zpk`.
2. **Vérification** — checksum SHA-256 de l'exécutable + signature ed25519
   du manifest (clé publique ConsoleOS intégrée au firmware).
3. **Copie** — extraction vers `/data/games/<id>/`.
4. **Indexation** — insertion/mise à jour dans la base SQLite locale
   (`/data/library.db`), ce qui évite tout scan complet au démarrage.
5. **Entrée bibliothèque** — apparition immédiate dans la grille de
   `consoleos-ui`, avec icône et jaquette.

## Création d'un paquet

Voir `src/gamemgr/mkzpk.py` :

```bash
python3 mkzpk.py create \
  --src ./mon_jeu/ --out MonJeu.zpk \
  --id com.studio.monjeu --name "Mon Jeu" --version 1.0.0 \
  --exec bin/monjeu --publisher "Studio" \
  --key cle_privee_signature.pem
```

Sans `--key`, le paquet est généré non signé — utilisable en développement
sur une image de test avec vérification de signature désactivée, mais
rejeté par une image de production standard.
