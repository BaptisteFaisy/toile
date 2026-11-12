# Scénario — Multi-écran virtuel (quatre têtes)

## Contexte utilisateur

L'utilisateur veut disposer de quatre écrans virtuels côte à côte dans
sa VM : une table de navigation à gauche, un document au centre, une
console à droite, un tableau de bord en bas. Chaque tête affiche une
ressource 2D distincte, à une position fixe.

## Fixture associée

`fixtures/multi-scanout.tcl` — quatre ressources 640 × 480, chacune avec
son backing, puis quatre `set_scanout` positionnés sur une surface
virtuelle 1280 × 960, puis un transfert et un flush par tête :

- `set_scanout scanout=0 id=1 x=0   y=0   width=640 height=480`
- `set_scanout scanout=1 id=2 x=640 y=0   width=640 height=480`
- `set_scanout scanout=2 id=3 x=0   y=480 width=640 height=480`
- `set_scanout scanout=3 id=4 x=640 y=480 width=640 height=480`

## Verdicts attendus

| # | Verdict | Sévérité |
|---|---|---|
| 0–3 | `TOILE_OK` (créations) | info |
| 4–7 | `TOILE_OK` (backings) | info |
| 8–11 | `TOILE_OK` (positionnements) | info |
| 12–15 | `TOILE_TRANSFER_OK` (transferts) | info |
| 16–19 | `TOILE_OK` (flush) | info |
| 20 | `TOILE_MODEL_CONSISTENT` | info |

Code de sortie attendu : **0**.

## Ce que cela prouve

- **P1 (cohérence)** — `TOILE_MODEL_CONSISTENT` : les quatre scanouts
  actifs pointent chacun une ressource existante et vivante (invariant I1) ;
  les quatre backings appartiennent à des ressources vivantes (I3) ; les
  tables restent dans leurs bornes (I5).
- **P2 (rejet)** — les quatre positionnements sont acceptés : le refus
  d'un scanout hors bornes (`scanout_id_out_of_range`) et le refus d'une
  table pleine (`scanout_table_full`) sont traités séparément, voir
  `fixtures/limites-tables.tcl`.
- **P3 (déterminisme)** — positions, rectangles et hachage d'état sont
  reproductibles à l'octet près ; deux exécutions de la fixture donnent
  le même hash FNV-1a 64 bits.

## Ce qui reste à valider sur cible réelle

- **E1** : l'invité QNX 8.0 sous QMX énumère-t-il `virtio-gpu-pci` sur son
  bus PCI ? Le multi-écran suppose un périphérique présent, pas simulé.
- **E2** : Screen accepte-t-il un display driver tiers ? Sans cette
  réponse, les quatre rectangles ne sont que des rectangles en mémoire.
- Comportement multi-têtes de l'invité : le pilote doit-il émettre quatre
  `set_scanout` distincts, et dans quel ordre ? Le modèle accepte
  n'importe quel ordre, mais la trace réelle doit être rejouée pour
  confirmer que l'ordre du pilote ne change pas le résultat.
