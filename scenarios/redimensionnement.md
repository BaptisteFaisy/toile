# Scénario — Redimensionnement de la fenêtre

## Contexte utilisateur

L'utilisateur agrandit la fenêtre de sa VM. Le guest doit créer un
framebuffer plus grand, republier la surface et le réafficher. Le
changement de taille se fait en remplaçant la ressource, pas en
modifiant ses dimensions : les dimensions d'une ressource 2D sont figées
à la création.

## Fixture associée

`fixtures/resize.tcl` — le redimensionnement dans l'ordre imposé par
`DECISIONS.md` D11 :

1. `resource_create_2d id=1 … width=64 height=48` + backing + scanout 0
2. `transfer_to_host_2d id=1` puis `resource_flush id=1` (état initial)
3. `set_scanout scanout=0 id=0 x=0 y=0 width=0 height=0` (désactivation)
4. `resource_unref id=1` (détrruction, désormais permise)
5. `resource_create_2d id=2 … width=128 height=96` + backing + scanout 0
6. `transfer_to_host_2d id=2` puis `resource_flush id=2` (nouvelle taille)

Le companion `fixtures/detruit-et-recreer.tcl` rejoue le même cycle en
réutilisant le même identifiant (`id=1`), pour prouver que le tableau des
ressources libère bien l'identifiant.

## Verdicts attendus

| # | Verdict | Sévérité |
|---|---|---|
| 0–2 | `TOILE_OK` (création, backing, scanout) | info |
| 3 | `TOILE_TRANSFER_OK` | info |
| 4 | `TOILE_OK` (flush) | info |
| 5 | `TOILE_OK` (`set_scanout id=0` : désactivation) | info |
| 6 | `TOILE_OK` (`resource_unref id=1` : plus de référence active) | info |
| 7–9 | `TOILE_OK` (création, backing, scanout sur `id=2`) | info |
| 10 | `TOILE_TRANSFER_OK` (rectangle `0,0,128,96`) | info |
| 11 | `TOILE_OK` (flush) | info |
| 12 | `TOILE_MODEL_CONSISTENT` | info |

Code de sortie attendu : **0**.

## Ce que cela prouve

- **P1 (cohérence)** — `TOILE_MODEL_CONSISTENT` : pendant toute la
  séquence, le scanout 0 ne référence jamais une ressource détruite
  (invariant I1) ; le `unref` n'intervient qu'après la désactivation.
- **P2 (rejet)** — le refus `TOILE_SCANOUT_INVALID` (ressource encore
  affichée) est bien distinct du refus `TOILE_RESOURCE_MISSING` ; voir
  `fixtures/fautes/faute-unref-affichee.tcl`. L'ordre D11 est donc
  obligatoire : un `unref` avant la désactivation serait refusé.
- **P3 (déterminisme)** — même séquence ⇒ même hachage d'état et mêmes
  verdicts ; le rectangle omis est développé en `0,0,largeur,hauteur` de
  la ressource, identiquement en C et en Python.

## Ce qui reste à valider sur cible réelle

- **E1** : l'invité QNX 8.0 sous QMX énumère-t-il `virtio-gpu-pci` ?
  Le resize suppose un périphérique présent.
- **E2** : Screen accepte-t-il un display driver tiers ? C'est la seule
  voie pour que la nouvelle taille soit visible à l'écran.
- Trace réelle : le pilote réels émet-il bien `set_scanout id=0` avant
  `resource_unref` ? Rejouer la capture sur le modèle doit donner le même
  état final qu'ici ; un écart trahit le pilote ou le modèle, jamais le
  harnais.
