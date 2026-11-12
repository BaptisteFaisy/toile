# Scénario — Curseur matériel (souris dans la VM)

## Contexte utilisateur

L'utilisateur bouge la souris dans la VM. Le curseur doit rester fluide et
ne pas faire recopier l'écran entier à chaque déplacement. Le pilote doit
donc piloter le curseur matériel de `virtio-gpu` (file `cursorq`) plutôt que
de redessiner l'image affichée.

## Fixture associée

`fixtures/curseur.tcl` — la séquence du curseur matériel :

1. `resource_create_2d id=1 … width=640 height=480` + backing + scanout 0
   (la surface affichée)
2. `resource_create_2d id=2 … width=64 height=64` + backing (l'image du
   curseur)
3. `update_cursor scanout=0 x=10 y=20 id=2 hot_x=0 hot_y=0` (rend le
   curseur visible et le place)
4. `move_cursor scanout=0 x=40 y=50` (déplace un curseur déjà visible)

La fixture `fixtures/fautes/faute-curseur-invisible.tcl` rejoue le
`move_cursor` sans `update_cursor` préalable : le refus
`cursor_not_visible` doit alors être émis.

## Verdicts attendus

| # | Verdict | Sévérité |
|---|---|---|
| 0–5 | `TOILE_OK` (créations, backings, scanout) | info |
| 6 | `TOILE_OK` (`update_cursor` : curseur visible) | info |
| 7 | `TOILE_OK` (`move_cursor` : curseur déjà visible) | info |
| 8 | `TOILE_MODEL_CONSISTENT` | info |

Code de sortie attendu : **0**.

## Ce que cela prouve

- **P1 (cohérence)** — `TOILE_MODEL_CONSISTENT` : l'état du curseur
  (ressource, tête, x, y, point chaud, visible) est sérialisé et haché
  dans l'état canonique ; le déplacement ne touche ni aux ressources ni
  aux scanouts, donc aucun invariant n'est mis en défaut.
- **P2 (rejet)** — `move_cursor` sans curseur visible est refusé avec la
  cause figée `cursor_not_visible` (voir la fixture fautive) : le refus
  est propre, l'état du curseur reste inchangé.
- **P3 (déterminisme)** — position finale du curseur et hachage d'état
  sont reproductibles ; la sérialisation §8 inclut la ligne `C`, donc le
  curseur entre dans la preuve de déterminisme.

## Ce qui reste à valider sur cible réelle

- **E1** : l'invité QNX 8.0 sous QMX énumère-t-il `virtio-gpu-pci` ?
- **E2** : Screen accepte-t-il un display driver tiers ? Le curseur
  matériel n'est visible qu'à travers la chaîne d'affichage.
- ** hors périmètre du modèle** : la fluidité réelle du pointeur (fréquence
  des `move_cursor`, file `cursorq` de Passerelle) et le comportement du
  compositeur d'écran côté QNX ne sont pas modelisés ici ; le modèle ne
  prouve que l'acceptation et la non-régression des commandes.
