# DECISIONS — journal des décisions de Toile

D1 à D9 sont les décisions de conception du cahier des charges (§15), recopiées
et enrichies. D10 à D16 sont les décisions prises **pendant l'implémentation**,
après le gel de [`CONTRACT.md`](../CONTRACT.md) et la relecture des en-têtes
standard `virtio_gpu.h` du 2026-11-12.

Chaque décision suit le même format : **contexte / décision / justification /
alternative écartée / conséquences**. Une décision n'est jamais rétroactive :
si le contrat change, le fichier `CONTRACT.md` et les deux implémentations
changent dans la même PR.

| # | Titre | Portée | Statut |
|---|---|---|---|
| D1 | Cœur C11 pur, séparé du driver | architecture | décidée |
| D2 | Réutiliser le transport virtio de Passerelle | architecture | décidée (E3 en cours) |
| D3 | Deux volets A (QEMU) / B (QNX Hypervisor), A d'abord | périmètre | décidée |
| D4 | Le sous-ensemble 15/12 = volet A minimal | planning | décidée |
| D5 | 2D d'abord, 3D documentée en piste X | périmètre | décidée |
| D6 | Validation du modèle contre QEMU réel | preuve | décidée (C8) |
| D7 | virtio-input en niveau C, pas S | périmètre | décidée |
| D8 | Licence MIT | juridique | décidée |
| D9 | Codelab inclus | livraison | décidée |
| D10 | CLI C dans `core/cli/`, hors de l'arborescence §4 | implémentation | décidée |
| D11 | `resource_unref` d'une ressource affichée **refusée** | contrat | décidée |
| D12 | Lecture hôte → invité exposée en `transfer_from_host` (0x0206) | contrat | décidée |
| D13 | `SET_CURSOR` (0x0302) non implémenté | contrat | décidée |
| D14 | Gardes CI en Python, pas de Node | CI | décidée |
| D15 | Rectangle omis = toute la ressource | contrat | décidée |
| D16 | Fail fast au premier contre-exemple | preuve | décidée |

---

## Décisions de conception (cahier des charges §15)

### D1 — Cœur C11 pur modélisant la machine à commandes, séparé du driver

- **Contexte** : le pilote invité et le display driver Screen ne peuvent être
  testés que sur une cible QNX, avec SDP et QEMU. Or le concours se joue en
  13 semaines et les essais E1/E2 ne sont pas tranchés.
- **Décision** : extraire la logique virtio-gpu (création de ressources,
  backing, scanout, flush, transferts, invariants) dans `core/`, en C11 pur,
  sans fichier, horloge, socket, API QNX ni allocation dynamique.
- **Justification** : testable sans QNX, sans QEMU, sans GPU ; comparable à un
  jumeau Python ; le driver réel n'est plus que de la colle au-dessus d'un noyau
  déjà prouvé. Méthode éprouvée sur Ambre, Passerelle, Tamis et Veille.
- **Alternative écartée** : écrire directement le pilote QNX et tester sur cible.
  Chaque itération coûterait un boot de VM, et aucune preuve ne survivrait à
  l'absence de SDP.
- **Conséquences** : une API gelée (`CONTRACT.md` §10), des limites fixes
  (64 ressources, 16 scanouts, 32 pages/ressource) et l'obligation de fournir la
  mémoire depuis l'appelant. Le pilote invité devra pré-allouer ses tampons.

### D2 — Réutiliser le transport virtio de Passerelle, ne pas le réécrire

- **Contexte** : le transport virtio pour QNX 8.0 (PCI moderne MSI-X, MMIO v1/v2,
  files virtuelles, négociation) existe déjà dans le projet frère Passerelle,
  écrit par le même auteur, sous Apache-2.0.
- **Décision** : Toile consomme ce transport et n'écrit que la couche virtio-gpu
  au-dessus (`guest/virtio/toile_transport.{h,c}`), plus un patch minimal
  (`toile-virtio-gpu.patch`) qui étend la découverte PCI/MMIO au type de
  périphérique 16 (DID moderne `0x1050`).
- **Justification** : zéro duplication ; la brique la plus difficile est déjà
  validée ; le patch est candidat à une contribution amont (X5).
- **Alternative écartée** : réécrire les virtqueues dans Toile. Effort élevé,
  risque de régression, et divergence entre deux transports du même portefeuille.
- **Conséquences** : dépendance externe au dépôt Passerelle (`PASSERELLE_DIR`) ;
  le patch s'applique à une **copie de build**, jamais à l'arbre partagé ;
  attribution Apache-2.0 conservée dans `NOTICE` et en-tête SPDX des fichiers
  dérivés. À trancher par l'essai E3 (compilation `qcc`, files multiples).

### D3 — Deux volets A (QEMU) / B (QNX Hypervisor), A d'abord

- **Contexte** : QEMU fournit déjà le périphérique `virtio-gpu-pci` ; l'hyperviseur
  QNX ne fournit pas de vdev virtio-gpu (G1), et écrire un vdev exige le DDK
  hyperviseur dont l'accès en licence non commerciale n'est pas vérifié (E4).
- **Décision** : volet A d'abord (invité QNX sous QEMU), volet B en niveau X.
- **Justification** : A ne dépend pas de E4 et livre la démo du concours ; B est
  un bonus dont le blocage est juridique avant d'être technique.
- **Alternative écartée** : mener les deux volets de front — le risque E4
  aurait contaminé le chemin critique du 15/12.
- **Conséquences** : `host-qnx/` reste vide tant que E4 n'est pas tranché ;
  toute la documentation et la CI visent QEMU.

### D4 — Le sous-ensemble 15/12 = volet A minimal (scanout unique, mire puis bureau)

- **Contexte** : 13 semaines de projet, 4 semaines avant le jalon du concours.
- **Décision** : S = virtio-gpu 2D minimal, scanout unique, flush, Screen basique
  en invité QEMU ; C et X après le jalon.
- **Justification** : tient dans 4 semaines et la démo reste spectaculaire (un
  bureau QNX dans une fenêtre QEMU, ce qui est impossible aujourd'hui — G3).
- **Alternative écartée** : viser multi-écran et curseur matériel avant le jalon.
- **Conséquences** : les exigences C1–C10 sont écrites dans le contrat mais non
  prouvées au 15/12 ; `docs/GAP.md` le dit explicitement.

### D5 — 2D d'abord, 3D (virgl/venus) documentée en piste X

- **Contexte** : virtio-gpu comporte un volet 3D (contexts, capsets, virgl, venus).
- **Décision** : Toile n'implémente que les commandes 2D du socle ; la 3D est une
  étude de faisabilité documentée (X3).
- **Justification** : la 3D triple l'effort ; le besoin réel (Screen, IHM
  embarquées, médicales) est 2D.
- **Alternative écartée** : un pilote 3D — hors calibre et hors catégorie visée.
- **Conséquences** : les commandes 3D (contexts, `RESOURCE_CREATE_BLOB` 0x010c,
  `RESOURCE_MAP_BLOB` 0x0208) renvoient `unknown_command_type` dans le modèle S ;
  la seule commande de la famille 3D retenue est le transfert de lecture 0x0206,
  parce qu'elle est aussi la lecture des ressources 2D (voir D12).

### D6 — Validation du modèle contre QEMU réel (capture/rejeu des commandes)

- **Contexte** : un modèle portable prouve sa propre cohérence, pas sa fidélité au
  périphérique réel.
- **Décision** : capturer les commandes émises par le pilote dans QEMU, les rejouer
  dans le modèle C, comparer les états (exigence C8).
- **Justification** : double preuve — modèle + comportement réel — comme Veille et
  Ambre ; un écart désigne un bug du modèle **ou** du driver, jamais du harnais.
- **Alternative écartée** : se contenter des fixtures écrites à la main (elles ne
  couvrent que ce qu'on a imaginé).
- **Conséquences** : le pilote doit être instrumentable et les fixtures « fil »
  (`fixtures/wire/*.hex`) doivent être rejouables telles quelles.

### D7 — virtio-input en niveau C, pas S

- **Contexte** : sans clavier ni souris, un bureau affiché n'est pas utilisable.
- **Décision** : l'affichage seul est le socle S ; virtio-input (idée « Main ») est
  en C5.
- **Justification** : l'affichage démontre déjà le trou comblé (G3) ; les entrées
  ajoutent un périphérique virtio de plus à écrire avant le jalon.
- **Alternative écartée** : clavier/souris dès S — aurait repoussé la démo.
- **Conséquences** : la démo du 15/12 est pilotée depuis l'hôte ou en lecture
  seule ; `guest/input/` reste vide au jalon.

### D8 — Licence MIT, cohérente avec le portefeuille

- **Contexte** : les autres projets du portefeuille sont MIT ou Apache-2.0.
- **Décision** : Toile est MIT (`LICENSE`), avec `NOTICE` pour les composants tiers.
- **Justification** : cohérence du portefeuille, compatibilité avec l'Apache-2.0 du
  transport repris (Apache-2.0 → MIT dans le sens de la redistribution des
  fichiers dérivés, en conservant leurs en-têtes SPDX).
- **Alternative écartée** : Apache-2.0 partout — plus lourd pour un dépôt de concours.
- **Conséquences** : les fichiers dérivés de Passerelle gardent SPDX Apache-2.0 ;
  les structures virtio-gpu dérivées des en-têtes standard gardent l'attribution
  BSD 3-clauses simplifiée.

### D9 — Codelab inclus

- **Contexte** : le concours propose un bonus Best Codelab/Tutorial.
- **Décision** : `codelab/` — « donner un affichage à une VM QNX » en 4 chapitres (C9).
- **Justification** : la pédagogie « VM QNX graphique » est forte et le trou (G3)
  est le plus cité de l'inventaire ; le codelab diffuse la méthode.
- **Alternative écartée** : ne livrer que du code — perd un bonus et une audience.
- **Conséquences** : un lot dédié (`codelab/`) écrit en parallèle de la documentation
  technique ; ce dernier dépend des lots L1–L7.

---

## Décisions d'implémentation (après gel du contrat, 2026-11-12)

### D10 — CLI C dans `core/cli/`, hors de l'arborescence §4 du cahier

- **Contexte** : le cahier des charges §4 ne prévoit pas de CLI en C : `core/`
  contient `include/`, `src/` et `tests/`. Or `CONTRACT.md` §11 gèle une CLI
  identique en C et en Python (`toile-run run|wire|golden|selftest` et
  `python3 -m toile.cli`), avec les mêmes codes de sortie 0/1/2/3.
- **Décision** : la CLI C vit dans `core/cli/`, à côté de `core/src/`, et non
  dans `core/src/` ni dans `core/tests/`.
- **Justification** : le test différentiel exige un **exécutable de référence**
  dont la sortie est comparée octet pour octet à celle du jumeau Python. Sans
  binaire C, la seule comparaison possible passerait par les tests unitaires,
  qui ne produisent pas les rapports `text`/`json`/`md` dans les conditions
  réelles d'appel (tampons, codes de sortie, arguments). `CONTRACT.md` §2
  n'autorise `printf` que dans `core/cli/` et dans les rapports : isoler la CLI
  garde le reste de `core/src/` muet (hors `report.c`), et la bibliothèque que
  liera le pilote invité n'embarque aucune sortie console.
- **Alternative écartée** : mettre `main()` dans `core/src/` — aurait cassé la
  règle « aucun `printf` hors de la CLI et des rapports » et rendu la
  bibliothèque liée au CLI non réutilisable depuis le pilote invité.
- **Conséquences** : `make build` produit `build/toile-run` ; la garde de taille
  s'applique à `core/cli/` comme au reste ; l'arborescence §4 du cahier est
  complétée (pas contredite) par ce répertoire.

### D11 — `resource_unref` d'une ressource affichée : **refusée**

- **Contexte** : le standard virtio-gpu laisse l'invité détruire une ressource
  alors qu'un scanout la référence encore. Deux comportements étaient possibles :
  laisser faire (et l'hôte affiche un scanout orphelin), ou refuser.
- **Décision** : `resource_unref` sur une ressource référencée par au moins un
  scanout actif est **refusée** — cause figée `resource_referenced_by_scanout`,
  état inchangé, verdict d'erreur.
- **Justification** : l'invariant **I1** (« tout scanout actif référence une
  ressource existante ») est la pierre angulaire de la preuve P1. Accepter la
  destruction créerait un état où un flush peut porter sur une ressource
  disparue — exactement le genre de bug d'affichage (écran figé, mémoire lue
  après libération) que le modèle existe pour empêcher.
- **Alternative écartée** : détacher implicitement les scanouts qui référençaient
  la ressource. Effet de bord silencieux, non observable dans la trace, et
  divergence garantie entre le C et le Python sur l'ordre de parcours.
- **Conséquences** : l'invité doit faire `set_scanout scanout=k id=0` (désactive
  le scanout) **avant** `resource_unref`. Les fixtures de resize suivent cet
  ordre ; une fixture fautive qui l'ignore sert de cas de test P2.

### D12 — Lecture hôte → invité exposée sous `transfer_from_host`, code 0x0206

- **Contexte** : le cahier des charges §2.3 citait `TRANSFER_FROM_HOST_2D`. La
  relecture de l'en-tête standard `virtio_gpu.h` du 2026-11-12 montre qu'il
  n'existe pas en virtio 1.x : la lecture hôte → invité est
  `VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D`, code **0x0206**, et elle s'applique
  aussi aux ressources créées en 2D.
- **Décision** : le DSL expose la commande sous le nom **`transfer_from_host`**
  (sans suffixe), mappée sur le code réel `0x0206`, acceptée pour toute
  ressource du modèle S.
- **Justification** : nommer la commande comme le standard la nomme (`…_3D`)
  laisserait croire qu'elle exige un contexte 3D ; l'appeler `…_2D` propagerait
  une commande inexistante. `transfer_from_host` dit ce qu'elle fait, et le code
  fil reste celui du standard — l'encodeur produit des octets qu'un périphérique
  virtio-gpu réel accepte.
- **Alternative écartée** : garder `transfer_from_host_2d` avec un code inventé.
  Rejeté : incompatible avec QEMU et faux documentaire.
- **Conséquences** : succès = `TOILE_OK` et non `TOILE_TRANSFER_OK` (réservé au
  sens invité → hôte, `CONTRACT.md` §6) ; un backing est exigé
  (`TOILE_BACKING_MISSING` sinon) ; le compteur `bytes_transferred` n'est
  incrémenté que pour les transferts vers l'hôte (invariant I6). Correction
  reportée au cahier des charges §13 et dans `docs/GAP.md`.

### D13 — `SET_CURSOR` (0x0302) non implémenté

- **Contexte** : le cahier des charges supposait une commande `SET_CURSOR` 0x0302.
  L'enum `virtio_gpu_ctrl_type` de l'en-tête standard **s'arrête à
  `VIRTIO_GPU_CMD_MOVE_CURSOR` (0x0301)** : 0x0302 n'est pas défini.
- **Décision** : Toile n'implémente aucun 0x0302. Le curseur passe par
  `update_cursor` (0x0300, rend le curseur visible et le place) et
  `move_cursor` (0x0301, déplace un curseur déjà visible).
- **Justification** : implémenter un code inexistant produirait des octets
  rejetés par QEMU et un DSL mentant sur le standard.
- **Alternative écartée** : conserver 0x0302 comme extension Toile. Rejeté : une
  extension privée dans un protocole standard n'a aucun bénéfice et empêche la
  validation contre QEMU réel (C8).
- **Conséquences** : `move_cursor` sans curseur visible → cause
  `cursor_not_visible` ; tout type hors table §3 → `unknown_command_type`. Le
  niveau S du DSL inclut déjà les deux commandes de curseur, ce qui prépare C2.

### D14 — Gardes CI en Python (`.github/checks/`), pas de Node

- **Contexte** : deux gardes sont exigées par `CONTRACT.md` §13 : taille des
  fichiers/fonctions et présence des signatures de l'API gelée. Le dépôt contient
  déjà Python (jumeau `host/`, `tests/differential.py`) et C ; il ne contient
  aucun JavaScript.
- **Décision** : les gardes sont écrites en Python stdlib —
  `.github/checks/taille.py` (< 500 lignes/fichier, < 50 lignes/fonction) et
  `.github/checks/api-frozen.py` (chaque signature de §10 présente dans les
  en-têtes) — plus `.github/ci-budget/check_timeouts.py` (`timeout-minutes` ≤ 5).
- **Justification** : zéro dépendance à installer sur le runner self-hosted, un
  seul langage d'outillage au lieu de deux, et les gardes se testent avec le
  même `unittest` que le jumeau.
- **Alternative écartée** : une garde Node (npm/action du marketplace) — ajoute
  un `node_modules`, une chaîne d'approvisionnement et du temps de CI pour deux
  scripts de 80 lignes.
- **Conséquences** : `make guards` appelle ces trois scripts ; le budget CI reste
  ≤ 5 min par job ; les gardes tournent sur le runner `toile-worker`, pas sur un
  runner mutualisé.

### D15 — Rectangle omis = « toute la ressource » (règle exacte §4)

- **Contexte** : `resource_flush`, `transfer_to_host_2d` et `transfer_from_host`
  acceptent un rectangle optionnel. Le standard interprète un rectangle nul comme
  « toute la ressource », mais la formulation exacte restait à geler pour que le C
  et le Python développent la valeur **au même moment**.
- **Décision** : un rectangle dont `width` **et** `height` valent 0 — omis ou
  écrit explicitement — est développé par le processeur en
  `x=0, y=0, width=resource.width, height=resource.height` **avant** application.
  `offset` vaut 0 par défaut.
- **Justification** : une seule règle, testable, identique des deux côtés ; le
  verdict `TOILE_TRANSFER_OK` porte alors le rectangle développé, donc la trace
  montre ce qui a vraiment été transféré (et I6, `bytes_transferred` = somme des
  aires × bpp, se vérifie sans deviner).
- **Alternative écartée** : développer le rectangle dans le rapport seulement, ou
  accepter `width==0` **ou** `height==0`. La première rend la trace menteuse ; la
  seconde confond « omis » et « bande dégénérée », et un rectangle de hauteur 0
  deviendrait un plein écran.
- **Conséquences** : une fixture peut écrire `resource_flush id=1` ou
  `resource_flush id=1 x=0 y=0 width=0 height=0`, le résultat est identique ;
  une création avec `width==0` ou `height==0` reste en revanche refusée
  (`bad_geometry`, invariant I4) — la règle du rectangle nul ne s'applique qu'aux
  commandes qui portent un rectangle, pas à la géométrie d'une ressource.

### D16 — Fail fast : la première violation d'invariant arrête la séquence

- **Contexte** : quand un invariant I1..I6 est violé, deux comportements étaient
  possibles — continuer en accumulant les violations, ou s'arrêter.
- **Décision** : la première violation **arrête** la séquence et produit
  `TOILE_MODEL_COUNTEREXAMPLE` (sévérité error) avec la cause
  (`invariant_i1` … `invariant_i6`) et l'état fautif. En fin de séquence propre,
  le processeur émet `TOILE_MODEL_CONSISTENT`.
- **Justification** : un contre-exemple est une information de débogage : sa
  valeur est dans le **préfixe minimal** qui le reproduit. Continuer après une
  violation noie la cause sous des violations dérivées et rend la comparaison
  C↔Python difficile (deux implémentations peuvent diverger sur l'état corrompu
  sans diverger sur le bug réel).
- **Alternative écartée** : mode « collecte » qui continue jusqu'au bout. Utile
  pour un fuzzer, mais il n'existe pas de fuzzer dans le socle S, et le mode
  aurait imposé de définir la sémantique d'un état incohérent — précisément ce
  que le contrat interdit.
- **Conséquences** : les fixtures fautives s'écrivent pour **un** défaut à la
  fois ; `toile-run run` renvoie le code de sortie 1 dès qu'un verdict d'erreur
  existe ; la réduction d'une divergence au plus court préfixe (comme sur
  Passerelle) devient triviale.
