Ce dossier doit contenir, à côté de theme.json :

  font.ttf        - police (ex. une police libre comme Inter ou Roboto,
                    à placer ici manuellement pour respecter sa licence)
  wallpaper.png   - fond d'écran 1280x720
  nav.wav         - son de navigation
  select.wav      - son de validation

Aucun de ces fichiers binaires n'est inclus dans ce dépôt de code source :
ce sont des ressources graphiques/sonores à fournir séparément (créées ou
sous licence libre), pas du code. consoleos-ui fonctionne sans eux (il
retombe sur une police système si aucune n'est trouvée) mais le thème sera
plus abouti une fois ces fichiers ajoutés.
