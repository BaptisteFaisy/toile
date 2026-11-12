# Chapitre 4 — Screen, la démo, et après

**Durée estimée : ~1 h 30**

## Objectif

Poser le dernier étage — le **display driver Screen** qui transforme le pilote
du chapitre 3 en affichage réel — puis dérouler la chaîne complète : mire
statique, bureau QNX, image invitée `mkifs`, scripts QEMU, capture pour la
vidéo de soumission (< 3 min), performances attendues (essai E5) et les
niveaux d'ambition C et X.

## Prérequis

- Chapitre 3 compris (pilote invité, `/dev/gpu0`, essai E1).
- SDP QNX 8.0 + QEMU hôte ; un invité QNX qui démarre en console série.
- **Screen Developer's Guide** du SDP installé à portée de main : c'est la
  source de vérité de l'étape 1.

> **⚠ Honnêteté — statut de ce chapitre.** L'essai **E2** (« Screen
> accepte-t-il un display driver tiers via son interface documentée ? ») n'est
> **pas encore tranché** : l'interface exacte du display driver (points
> d'entrée, bibliothèque, contrat d'initialisation) doit être relue **page à
> page** dans le Screen Developer's Guide du SDP installé avant d'être gelée
> (cahier des charges §13). Tout ce qui s'exécute dans l'invité QNX ci-dessous
> est **à valider sur cible** ; les verdicts E1/E2/E5 seront consignés dans
> `docs/TRIALS-E1-E4.md` (lot « code invité », en cours d'écriture). Les
> performances d'E5 sont une **question ouverte**, pas une promesse.

---

## Étape 1 — L'interface display driver de Screen (essai E2)

Screen est le serveur graphique de QNX : les applications dessinent via
`libscreen`, Screen compose, et un **display driver** envoie les pixels au
périphérique d'affichage. C'est le maillon qui manque en VM (trous G2/G3) et
que Toile écrit : un display driver dont le « périphérique d'affichage » est
le pilote virtio-gpu du chapitre 3 (`/dev/gpu0`).

Ce que l'on sait à la date d'écriture (cahier des charges §2.1, §10) :

- Screen expose une interface de display driver documentée (le guide évoque
  `io-display` et des bibliothèques de display driver) — **le contrat exact
  est l'objet de l'essai E2** : quels points d'entrée implémenter, comment le
  driver est découvert et chargé, quels formats de framebuffer sont acceptés.
- E2 est **bloquant pour l'exigence S7** (Screen basique en invité QEMU) :
  sans display driver tiers possible, la promesse « bureau QNX en VM » tombe.
- Méthode imposée par le cahier des charges : lire le Screen Developer's
  Guide **page à page** dans le SDP installé, consigner les références exactes
  (pages, prototypes) dans `docs/TRIALS-E1-E4.md` et `docs/VIRTIO-GPU.md`,
  puis seulement écrire `guest/display/`.

**Travail concret E2** *(à valider sur cible)* : construire le display driver
le plus bête possible — une tête, un framebuffer fixe, aucun mode dynamique —
et vérifier que Screen l'accepte et lui confie des pixels. Si ce squelette
passe, le reste n'est que de la mécanique.

## Étape 2 — Un framebuffer, une tête

Vocabulaire du display driver, aligné sur le contrat virtio-gpu :

| Côté Screen | Côté virtio-gpu (chapitres 1–3) |
|---|---|
| **Framebuffer** : tampon de pixels du bureau | une **ressource** + son **backing** (pages invité) |
| **Tête** (display/head) : un écran | un **scanout** (`set_scanout scanout=N id=R …`) |
| Format de pixels (ex. 32 bits/pixel) | `toile_fmt` du contrat (§3), tous à 4 octets/pixel au niveau S |
| Rafraîchir l'écran | `transfer_to_host_2d` (rectangles modifiés) puis `resource_flush` |

Le niveau **S** (socle, jalon 15/12) se limite volontairement à : **un seul
scanout**, une résolution fixe, pas de curseur matériel, pas de double tampon.
C'est la plus courte ligne droite entre E1/E2 et la première image affichée.

Dimensionnement utile (le calcul tombe juste) : un framebuffer 1920×1080 en
4 octets/pixel = 8 294 400 octets ≈ 7,9 Mio, soit exactement **2025** pages
de 4096 octets. Rappel du modèle (chapitre 2) : `TOILE_MAX_PAGES` = **32
entrées** par ressource — chaque entrée `addr:len` pouvant couvrir un grand
segment contigu, un framebuffer réel s'encode en quelques entrées, pas 2025.

## Étape 3 — La mire statique : la première lumière

Premier jalon visuel du projet (semaine S4 du planning) : **une mire de
barres de couleurs**, dessinée une fois, affichée pour toujours. Chaîne
complète *(à valider sur cible)* :

1. Le pilote (chapitre 3) a énuméré le périphérique et ouvert `controlq`.
2. Le display driver alloue un framebuffer invité et déclare la ressource :
   `resource_create_2d` + `resource_attach_backing`.
3. Il affecte la tête : `set_scanout scanout=0 id=1 …` (résolution du framebuffer).
4. Il remplit le framebuffer avec la mire (quelques `memset` colorés).
5. Il pousse et affiche : `transfer_to_host_2d` (toute la ressource) puis
   `resource_flush`.
6. **La mire apparaît dans la fenêtre QEMU.**

Pourquoi une mire et pas directement le bureau : elle découple les risques.
Si la mire s'affiche, E1 (énumération), E3 (transport) et le chemin
transfer/flush sont prouvés **indépendamment de Screen**. Si le bureau ne
s'affiche pas ensuite, le coupable est E2 — une seule inconnue à la fois.

Vérification *(à valider sur cible)* : capture d'écran de la fenêtre QEMU ;
côté hôte, les compteurs du modèle rejoué (chapitre 2) doivent raconter la
même histoire : `applied`, `flushes=1`, `bytes=8294400` pour un plein cadre
1080p.

## Étape 4 — Le bureau QNX

La mire validée, on branche Screen *(à valider sur cible)* :

1. Démarrer le serveur Screen dans l'invité avec le display driver Toile —
   **mécanisme exact = objet d'E2** (chargement de la bibliothèque, fichier de
   configuration, nom du périphérique `/dev/gpu0`).
2. Observer : le **bureau QNX s'affiche dans la fenêtre QEMU** — c'est la
   promesse du projet en une image (cahier des charges §0).
3. Lancer une **application Screen réelle** en invité (exigence C6) : elle
   dessine via `libscreen` → Screen compose → display driver Toile →
   `transfer_to_host_2d` des rectangles modifiés → `resource_flush`.
4. Les entrées clavier/souris arrivent par la console série au niveau S ; le
   confort (virtio-input, G4) est au niveau C — étape 9.

Chaque pixel affiché à ce stade est la preuve vivante de la chaîne complète :
standard virtio-gpu (OASIS) → QEMU hôte → pilote invité → cœur portable →
display driver → Screen → application.

## Étape 5 — L'image invitée `mkifs`

Pour que la démo soit reproductible (exigence **C10**), l'invité QNX est une
**image `mkifs`** construite depuis le dépôt *(à valider sur cible)* :

- le buildfile vit dans `image/` ; il assemble : noyau, pilotes de
  démarrage, le pilote Toile (`/dev/gpu0`), le display driver Screen, la
  configuration Screen, un shell — de quoi booter et afficher ;
- `mkifs` produit l'image de boot (`ifs.bin`) ; la même image sert à la
  sonde E1, à la mire et à la démo finale ;
- règle du portefeuille : image **reproductible** — mêmes entrées, même
  image ; la recette exacte sera documentée avec les scripts (étape 6).

## Étape 6 — Les scripts QEMU du volet A

Trois scripts (répertoire `qemu/`, lot « scripts QEMU » — à venir) couvrent
tous les lancements de ce codelab *(commandes à valider sur cible)* :

| Script | Rôle |
|---|---|
| `qemu/run-toile.sh` | lancement de référence du volet A : VM QNX + `-device virtio-gpu-pci` + console série + image `mkifs` — la commande du chapitre 1, étape 6, figée et paramétrable |
| `qemu/variants.sh` | matrice de variantes (mémoire, SMP, variante MMIO du périphérique, avec/sans accélération hôte) pour les essais et le débogage |
| `qemu/probe-e1.sh` | sonde dédiée à l'essai E1 : démarre, énumère le PCI invité, consigne la sortie brute pour `docs/TRIALS-E1-E4.md` |

Cible Makefile associée (gelée, `CONTRACT.md` §12) : `make qemu-demo` —
« volet A : documenté, exige SDP QNX + QEMU, **jamais appelé en CI PR** ». La
CI des PR reste sur `make test` (chapitre 2) : le build E2E QEMU est hors PR,
conformément au budget CI de 5 minutes.

## Étape 7 — La démo et la vidéo de soumission (< 3 min)

Structure imposée par le cahier des charges §9 — chaque séquence a sa capture
*(captures à produire une fois les jalons validés sur cible)* :

| Temps | Séquence | Capture attendue |
|---|---|---|
| 0:00–0:25 | **Le problème** : la VM QNX démarre, aucun affichage — console série seulement (G3) | fenêtre QEMU noire + terminal série qui défile |
| 0:25–1:00 | **Toile** : la même VM + le pilote → le bureau QNX s'affiche ; une application Screen réelle est lancée | fenêtre QEMU avec le bureau (LE plan du projet) |
| 1:00–1:40 | **Le cœur portable** : sur PC, sans QNX, `make test` rejoue les séquences, P1–P3 verts, différentiel C↔Python vert | terminal hôte : sortie de `make test` |
| 1:40–2:20 | **Validation croisée** (C8) : commandes capturées du pilote réel, rejouées dans le modèle C — même état | deux terminaux : trace QEMU / verdicts du modèle, mêmes hash |
| 2:20–3:00 | **Niveau C** : multi-écran virtuel, curseur, suivi de dommages | fenêtre QEMU multi-têtes + curseur qui bouge |

Règles de soumission (concours) : **pas de musique sous copyright, aucune
marque tierce** — « virtio », « OASIS », « QEMU », « QNX » sont les références
du projet ; visuel : fenêtre QEMU + terminal hôte.

## Étape 8 — Performances attendues et essai E5

**E5** (non bloquant — il ajuste les ambitions du niveau C) : un flush 2D via
virtio-gpu (copie du framebuffer vers l'hôte) est-il suffisant pour un bureau
utilisable, soit **≥ 15–20 images/s en 1080p** ?

Ordres de grandeur *(calculs, pas des mesures — à valider sur cible)* :

- plein cadre 1080p × 4 octets = 8,29 Mo par image ; à 20 img/s plein cadre
  permanent : ~166 Mo/s de copie invité→hôte. C'est beaucoup — QEMU l'encaisse
  généralement, mais ce n'est **pas** le régime réel ;
- le régime réel, c'est le **suivi de dommages** (C3) : seuls les rectangles
  modifiés sont transférés. Un terminal qui défile ou un curseur qui bouge
  touche quelques pourcents de l'écran : à 5 % de dommage, ~8,3 Mo/s à
  20 img/s — deux ordres de grandeur sous le plein cadre ;
- mesure prévue : compteurs du modèle (`flushes`, `bytes` — chapitre 2, §8)
  + chronométrage hôte des fenêtres QEMU ; le verdict E5 ira dans
  `docs/TRIALS-E1-E4.md` avec la procédure exacte.

Si E5 tombe sous le seuil en plein cadre : on réduit l'ambition (résolution,
fréquence) et on priorise C3/C4 (dommages, double tampon) — c'est précisément
à ça que sert un essai non bloquant.

## Étape 9 — Et après : niveaux C et X

| Niveau | Contenu | État |
|---|---|---|
| **C** (complet, ~3 mois) | C1 multi-scanout, C2 curseur matériel (`cursorq`), C3 suivi de dommages, C4 double tampon + vsync, C5 virtio-input (clavier/souris — comble G4, rend le bureau **utilisable**), C6 applications Screen réelles, C7 `/dev/gpu0` typé, C8 validation contre QEMU réel, C9 ce codelab, C10 image `mkifs` reproductible | visé S5–S10 du planning |
| **X** (extrême, documenté, hors concours) | X1 **volet B** : vdev virtio-gpu côté hôte QNX Hypervisor (si E4 tranche l'accès au DDK hyperviseur en licence non commerciale — **non vérifié**), X2 accélération 2D côté hôte, X3 piste 3D virgl/venus (étude de faisabilité), X4 blob resources, X5 contribution amont (retour à Passerelle) | documenté seulement |

## Exercices

1. **Storyboard.** Chronométrez la vidéo §9 séquence par séquence avec votre
   propre matériel : quels plans peuvent être tournés **dès aujourd'hui**
   (chapitre 2 : `make test` sur PC) et lesquels attendent la cible (bureau,
   multi-têtes) ? Que montre-t-on à 1:00–1:40 si E2 n'est pas encore tranché ?
2. **Bande passante.** Calculez le débit invité→hôte pour : (a) plein cadre
   1080p à 15 puis 20 img/s ; (b) un curseur 64×64 qui bouge à 60 img/s ;
   (c) une vidéo plein écran à 30 img/s. Lequel de ces trois régimes rend C3
   (dommages) indispensable ? Lequel rend C4 (double tampon) pertinent ?
3. **Encodage du backing.** Écrivez la ligne DSL `resource_attach_backing`
   d'un framebuffer 1080p contiguous, en respectant `TOILE_MAX_PAGES` = 32
   (chapitre 2, étape 3). Combien d'entrées faut-il au minimum ? (Une seule :
   `pages=0x…:8294400` — la limite porte sur le **nombre d'entrées**, pas sur
   leur taille.)
4. **Arbre des preuves.** Listez ce que la mire statique (étape 3) prouve et
   ce qu'elle ne prouve **pas**. (Elle prouve E1 + transport + transfer/flush ;
   elle ne prouve ni E2, ni Screen, ni les performances.)

## Points de contrôle

- [ ] Énoncer l'essai E2, ce qu'il bloque (S7), et où son contrat exact sera consigné.
- [ ] Expliquer la correspondance framebuffer/ressource, tête/scanout, rafraîchir/transfer+flush.
- [ ] Justifier la mire statique comme jalon intermédiaire (découplage des risques E1/E3 vs E2).
- [ ] Citer les trois scripts QEMU et la règle « `make qemu-demo` jamais en CI PR ».
- [ ] Restituer la structure de la vidéo (< 3 min, cinq séquences) et les deux interdits (musique sous copyright, marque tierce).
- [ ] Donner l'ordre de grandeur plein cadre 1080p à 20 img/s (~166 Mo/s) et pourquoi le suivi de dommages change tout.

## Erreurs fréquentes

| Symptôme | Cause probable | Remède |
|---|---|---|
| Fenêtre QEMU noire alors que le pilote tourne | display driver non chargé par Screen, ou scanout non affecté | vérifier dans l'ordre : E1 (périphérique vu), mire (transfer/flush), E2 (Screen accepte le driver) |
| Screen démarre mais rien à l'écran | framebuffer jamais transféré (pas de `transfer_to_host_2d` avant `resource_flush`) | le flush affiche ce que l'hôte **a reçu** : transférer d'abord |
| Image `mkifs` trop grosse ou qui ne boote pas | modules manquants/excédentaires dans le buildfile | itérer sur `image/` avec `qemu/variants.sh` ; consigner la recette reproductible |
| Bureau à 2 img/s | régime plein cadre sans suivi de dommages | mesurer (étape 8), puis C3 : ne transférer que les rectangles modifiés |
| Vidéo refusée à la soumission | > 3 min, musique sous copyright ou marque tierce visible | respecter §9 ; les seules références sont virtio/OASIS/QEMU/QNX |
| Présenter une étape « à valider » comme testée | enthousiasme | règle du codelab : seul `docs/TRIALS-E1-E4.md` transforme un « à valider » en « vérifié » |
