# Format de thème ConsoleOS

Chaque thème est un dossier sous `/usr/share/consoleos/themes/<nom>/`
contenant un `theme.json` et ses ressources. **Aucune couleur n'est codée
en dur dans `consoleos-ui`** : tout provient de ce fichier (avec un
repli sûr sur des valeurs par défaut si un champ est absent).

```json
{
  "name": "Mon Thème",
  "bg":        [18, 18, 24, 255],
  "panel":     [30, 30, 40, 235],
  "accent":    [88, 166, 255, 255],
  "text":      [240, 240, 245, 255],
  "text_dim":  [150, 150, 160, 255],
  "highlight": [255, 180, 60, 255],
  "font": "font.ttf",
  "wallpaper": "wallpaper.png",
  "sound_nav": "nav.wav",
  "sound_select": "select.wav"
}
```

Couleurs au format `[r, g, b, a]` (0-255). `font`, `wallpaper`,
`sound_nav`, `sound_select` sont des chemins relatifs au dossier du thème.

L'utilisateur peut créer autant de thèmes que voulu et basculer entre eux
depuis le menu Paramètres de `consoleos-ui`, qui relit simplement un autre
dossier de thème au redémarrage de l'interface.
