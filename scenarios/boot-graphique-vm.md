# Scénario — Boot graphique d'une VM invitée

## Contexte utilisateur

Au démarrage d'une VM QNX 8.0 sous QEMU, l'écran reste noir : rien
n'est affiché. L'utilisateur voit le périphérique `virtio-gpu-pci` dans
le gestionnaire de périphériques de l'invité, mais aucune image n'apparaît.
Il veut comprendre, avant de toucher au pilote, si la séquence de commandes
que le pilote est censé envoyer est acceptée par le modèle et si l'état
final est cohérent.

## Fixture associée

`fixtures/boot-graphique.tcl` — la séquence canonique du pilote :

1. `resource_create_2d id=1 format=B8G8R8A8_UNORM width=1280 height=800`
2. `resource_attach_backing id=1 pages=0x1000:4096`
3. `set_scanout scanout=0 id=1 x=0 y=0 width=1280 height=800`
4. `transfer_to_host_2d id=1` (rectangle omis → rectangle entier de la ressource)
5. `resource_flush id=1`

## Verdicts attendus

| # | Verdict | Sévérité |
|---|---|---|
| 0 | `TOILE_OK` | info |
| 1 | `TOILE_OK` | info |
| 2 | `TOILE_OK` | info |
| 3 | `TOILE_TRANSFER_OK` (rectangle `0,0,1280,800`) | info |
| 4 | `TOILE_OK` | info |
| 5 | `TOILE_MODEL_CONSISTENT` | info |

Code de sortie attendu : **0**.

## Ce que cela prouve

- **P1 (cohérence)** — `TOILE_MODEL_CONSISTENT` : après chaque commande
  les invariants I1 à I6 tiennent ; la ressource existe, le scanout pointe
  une ressource vivante, le backing appartient à une ressource vivante.
- **P2 (rejet)** — la séquence est acceptée intégralement, sans refus ni
  application partielle ; l'état final contient 1 ressource, 1 scanout,
  1 flush, 4 096 000 octets transférés (1280 × 800 × 4 octets/pixel).
- **P3 (déterminisme)** — deux exécutions donnent le même hachage d'état
  FNV-1a 64 bits et la même trace de verdicts ; aucune horloge n'intervient.

## Ce qui reste à valider sur cible réelle

- **E1** : l'invité QNX 8.0 sous QMX (`-device virtio-gpu-pci`) énumère-t-il
  le périphérique sur son bus PCI ? Sans E1, rien de ce qui précède ne
  concerne la vraie machine.
- **E2** : Screen accepte-t-il un display driver tiers via son interface
  documentée ? C'est la condition pour que le résultat du flush parvienne
  à l'écran de l'utilisateur.
- Rejeu de la trace réelle : la capture des commandes émises par le pilote
  (instrumentation ou trace QEMU) doit être rejouée dans le modèle et
  donner le même état final qu'ici. Tout écart est un bug du modèle ou du
  pilote, jamais du harnais.
