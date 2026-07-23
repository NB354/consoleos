# Protocole IPC interne ConsoleOS

Tous les démons communiquent par sockets UNIX (`SOCK_STREAM`) avec des
messages JSON, une ligne = un message (`\n` terminal). Volontairement
simple pour rester léger sur 1 Go de RAM (pas de DBus).

## Sockets

| Socket | Démon | Rôle |
|---|---|---|
| `/run/consoleos/notify.sock` | `consoleos-notifyd` | Bus de notifications (pub/sub) |
| `/run/consoleos/pad.sock` | `consoleos-padd` | Événements manettes normalisés |
| `/run/consoleos/gamemgr.sock` | `consoleos-gamemgrd` | Gestion de la bibliothèque de jeux |
| `/run/consoleos/update.sock` | `consoleos-updated` | État des mises à jour |

## consoleos-gamemgrd

Requête → Réponse (une ligne JSON, sauf `LIST` qui envoie plusieurs lignes
entre `list_begin` et `list_end`) :

```
LIST
INSTALL /media/usb0/MonJeu.zpk
UNINSTALL com.studio.monjeu
LAUNCH com.studio.monjeu
```

## consoleos-notifyd

Un client s'abonne en envoyant `SUBSCRIBE`. Tout autre message reçu par le
démon est rediffusé (fan-out) à tous les abonnés :

```json
{"type":"update_available","text":"Mise à jour disponible","level":"info"}
{"type":"install_complete","text":"Mon Jeu","level":"info"}
```

## consoleos-padd

Diffuse en continu (broadcast vers `consoleos-notifyd`, relayé à l'UI) des
événements normalisés :

```json
{"pad":0,"driver":"xbox","type":"button","name":"A","value":1}
{"pad":0,"driver":"xbox","type":"axis","name":"LSTICK_X","value":128}
```

Le champ `driver` identifie le pilote utilisé (`generic`, `xbox`, `ds4`,
`dualsense`, `switch_pro`), ce qui permet à l'UI et aux jeux d'adapter
l'affichage des boutons (glyphes) sans se soucier du modèle exact.
