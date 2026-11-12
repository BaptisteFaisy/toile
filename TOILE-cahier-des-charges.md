# Toile — pilote virtio-gpu et intégration Screen pour QNX 8.0 en invité

**Nom de travail : « Toile »** (la surface sur laquelle l'invité QNX peint son bureau, tendue entre le guest et l'hôte).
Version 0.1 — 12 novembre 2026 — projet candidat au QNX Global Hackathon, catégorie **Low-level Software**.
Classement interne : **1ᵉʳ du reclassement à 3 mois** (v4 du dossier `qnx-dossier`), originalité 5, trou le plus cité de tout l'inventaire.
Calibre : **3 mois** (13 semaines), avec un **sous-ensemble livrable au 15/12/2026** pour le concours.
Visé explicitement : **Best of Low-level Software**, bonus **Best use of QNX-specific Technologies** (resource managers, DDK, microkernel, Screen).

> Note de nommage : « Toile » est un nom commun français, aucune marque de tiers. Le règlement proscrit les marques de tiers dans les supports de soumission (vidéo comprise) — aucun nom de produit GPU réel (Intel, NVIDIA, AMD, ARM, Virtio est un standard ouvert OASIS, pas une marque) n'apparaît dans les supports de soumission.

---

## 0. Lecture rapide

**Ce que c'est.** Un **pilote virtio-gpu 2D** pour QNX 8.0 en **invité** (VM sous QEMU ou sous QNX Hypervisor), accompagné d'un **display driver Screen** qui s'appuie dessus. Résultat : `Screen` — le serveur graphique de QNX — fonctionne dans une VM **sans passthrough matériel de GPU**, ce qui est impossible aujourd'hui.

**La promesse, en une phrase.** Un développeur QNX démarre une VM sur son portable, et voit un **bureau QNX complet** s'afficher dans une fenêtre — sans carte graphique dédiée, sans passthrough, sans seconde machine.

**Trois idées qui portent le projet.**

1. **Le transport virtio existe déjà dans le portefeuille ; le périphérique virtio-gpu, non.** Passerelle (projet frère) a déjà écrit un transport virtio (PCI moderne MSI-X et MMIO v1/v2) pour QNX 8.0. Toile **réutilise ce transport** et n'écrit que la couche spécifique virtio-gpu (files d'attente `controlq`/`cursorq`, commandes 2D, scanout). On ne réinvente pas virtio ; on ajoute le périphérique manquant.
2. **Deux volets, un seul risque à trancher tôt.** *Volet A* (QEMU hôte) : QEMU fournit déjà le périphérique `virtio-gpu-pci` ; il ne manque que le **driver invité QNX** → pas besoin du DDK hyperviseur. *Volet B* (QNX Hypervisor hôte) : il faut **aussi** écrire le **vdev** côté hôte, ce qui exige le DDK hyperviseur dont l'accès en licence non commerciale n'est **pas vérifié** (essai E4, bloquant pour B seulement). Le sous-ensemble 15/12 = volet A.
3. **Le protocole virtio-gpu est modélisable en C11 pur sur PC.** Le cœur du projet — la machine à commandes virtio-gpu (création de ressources, attachement de backing, scanout, flush, transfert hôte) — est une logique portable qui se teste **sans QNX, sans QEMU, sans GPU** : on alimente le modèle avec des séquences de commandes et on vérifie les états produits. Le driver réel n'est que la colle au-dessus de ce modèle. C'est la méthode « cœur portable + jumeau » déjà éprouvée sur Ambre, Passerelle, Tamis et Veille.

**Trois niveaux d'ambition.** **S** (socle : virtio-gpu 2D minimal, scanout unique, flush, Screen basique en invité QEMU — livrable 15/12), **C** (complet : multi-scanout, curseur matériel, suivi de dommages, double tampon, vsync, virtio-input clavier/souris, intégration Screen complète), **X** (extrême : vdev virtio-gpu côté hôte QNX Hypervisor, accélération 2D côté hôte, piste 3D virgl/venus documentée).

**À valider avant d'écrire la moindre ligne de produit** (section 10) : cinq essais, dont E1 — un invité QNX sous QEMU énumère-t-il le périphérique `virtio-gpu-pci` sur son bus PCI ? — et E2 — Screen accepte-t-il un display driver tiers via son interface documentée ?

**Statut des affirmations.** Section 13 : l'interface exacte du display driver Screen et le format des structures virtio-gpu de QEMU doivent être relus page à page dans le SDP et les sources QEMU avant d'être gelés. Ce sont les premiers travaux, pas des hypothèses de plus.

---

## 1. Contexte et cas d'usage

### 1.1 Qui a ce problème

| Acteur | Besoin | Ce qu'il fait aujourd'hui |
|---|---|---|
| Développeur QNX sans matériel cible | Développer et déboguer une IHM Screen sur son portable | Pas de solution graphique en VM : console série uniquement, ou achat d'une carte |
| Intégrateur cockpit/cluster (automotive) | Tester une stack graphique complète avant d'avoir l'ECU | Passthrough GPU lourd et spécifique à l'hyperviseur, ou matériel dédié |
| Enseignant / étudiant QNX | Montrer Screen et les applications graphiques dans un TP | Impossible en VM standard ; QNX reste « l'OS qu'on ne voit pas tourner » |
| Ingénieur test/CI | Exécuter des tests d'IHM Screen dans une ferme de VM | Aucun affichage ; les tests graphiques exigent du matériel |
| Fabricant de dispositif médical | Valider une IHM certifiable en environnement isolé | Pas de graphique en VM = pas de validation sur poste standard |

### 1.2 Pourquoi c'est un vrai marché

QNX équipe des cockpits numériques, des clusters, des IHM médicales et industrielles — **toutes** ces applications utilisent Screen. Or le cycle de développement moderne passe par la VM (QEMU, hyperviseur) pour itérer vite, faire de la CI, et développer sans matériel. Aujourd'hui, dès qu'on a besoin de graphique, la VM ne suit pas : il faut du passthrough (complexe, lié à l'hôte, non portable) ou du matériel physique. **Toile lève ce blocage** : le graphique QNX devient disponible en VM, sur n'importe quel poste, avec le périphérique virtuel standard de l'industrie (virtio-gpu).

C'est aussi un **prérequis** pour d'autres projets du portefeuille : une VM QNX avec affichage permet de démontrer Veille (alarmes visuelles), Ambre (outils de lecture), Tamis (visualiseur de graphes) directement en invité.

### 1.3 Pourquoi maintenant

- QNX 8.0 est gratuit en usage non commercial (QNX Everywhere, licence en ~1 h).
- QEMU fournit un périphérique `virtio-gpu-pci` mature (utilisé par Linux, BSD) : la moitié hôte du volet A existe déjà.
- Le transport virtio pour QNX 8.0 est déjà écrit et validé dans **Passerelle** : la brique la plus difficile (PCI moderne, MSI-X, files virtio) est disponible.
- Le concours cible explicitement QEMU et Raspberry Pi 5 : la démo en VM est dans le cadre.

### 1.4 Ce que Toile n'est pas

Ni un pilote de GPU physique (Intel/NVIDIA/AMD/ARM), ni une implémentation 3D complète (virgl/venus sont hors socle, documentés en piste X), ni un serveur d'affichage à distance (cf. l'idée « Vitrine » du lot D), ni un compositeur Wayland (cf. « Compositeur », lot D). Toile est le **maillon manquant** entre le périphérique virtuel virtio-gpu et le serveur Screen de QNX.

---

## 2. Ancrage documentaire

### 2.1 Ce que QNX fournit déjà (à ne pas réinventer)

| Élément | Rôle pour Toile |
|---|---|
| **Screen** (`libscreen`, serveur Screen, `io-display`) | Le serveur graphique cible ; expose une interface de display driver à implémenter |
| **DDK** (Driver Development Kit) | Kit pour écrire le pilote de périphérique virtio-gpu côté invité |
| **Transport virtio de Passerelle** (projet frère) | virtio-pci moderne (MSI-X) + virtio-mmio v1/v2 déjà écrits et testés pour QNX 8.0 |
| **Resource managers** (`resmgr_*`) | Pour exposer le périphérique dans l'espace de noms (`/dev/gpu*`) et dialoguer avec Screen |
| **`pci` / `libpci`** | Énumération et configuration du périphérique PCI virtio-gpu en invité |
| **QNX Hypervisor + vdev** | Côté hôte (volet B) : le framework de périphériques virtuels (~20 vdev livrés, **virtio-gpu absent**) |
| **QEMU** | Hôte du volet A : fournit `virtio-gpu-pci` et `virtio-gpu-device` (MMIO) prêts à l'emploi |

### 2.2 Les trous exploités (à sourcer page à page — section 13)

| Code | Trou, tel que formulé dans l'inventaire | Source |
|---|---|---|
| **G1** | **virtio-gpu absent** des ~20 vdev livrés avec QNX Hypervisor | Inventaire lot D (liste des vdev vérifiée) |
| **G2** | **Aucun display driver Screen pour virtio-gpu** en invité : Screen ne sait pas parler à un GPU virtuel | Inventaire lot D |
| **G3** | **Screen inutilisable en invité sans passthrough** matériel — le trou le plus cité de l'inventaire | Inventaire lot D |
| **G4** | **virtio-input absent** (clavier/souris en VM) : même avec l'affichage, les entrées manquent | Inventaire lot D (idée « Main ») |
| **G5** | **Aucune virtualisation GPU documentée** dans la doc publique QNX 8.0 | Inventaire |
| **G6** | Pas de multi-écran virtuel : un invité ne peut pas piloter plusieurs têtes sans matériel | Conséquence de G1–G3 |

> Honnêteté : G1–G6 sont des affirmations d'**absence** dans la documentation publique et la liste des vdev livrés. Comme pour Tamis et Veille, elles doivent être revérifiées page à page dans le SDP installé avant d'être gravées dans la soumission. Section 13.

### 2.3 Le standard virtio-gpu (référence externe)

Le protocole virtio-gpu est spécifié par OASIS (virtio 1.x, section « GPU Device »). Points clés utilisés par Toile :
- Files virtuelles : `controlq` (commandes), `cursorq` (curseur), et en 3D `virgl` (hors socle).
- Commandes 2D : `RESOURCE_CREATE_2D`, `RESOURCE_UNREF`, `RESOURCE_ATTACH_BACKING`, `RESOURCE_DETACH_BACKING`, `SET_SCANOUT`, `RESOURCE_FLUSH`, `TRANSFER_TO_HOST_2D`, `TRANSFER_FROM_HOST_2D`, `RESOURCE_CREATE_BLOB`/`RESOURCE_MAP_BLOB` (virtio-gpu moderne, blob resources).
- Le guest crée des **ressources** (surfaces) identifiées par `resource_id`, y attache son **backing storage** (pages invité), définit le **scanout** (quelle ressource va sur quelle tête, à quelle position), puis demande des **flush**/transferts pour que l'hôte affiche.

---

## 3. Périmètre

### 3.1 Dans le périmètre

- **Cœur portable** (`core/`, C11 pur) : modèle de la machine à commandes virtio-gpu 2D — état des ressources, scanouts, backing, flush/transferts ; validation de séquences de commandes ; générateur de traces de commandes pour tests.
- **Driver invité** (`guest/`, QNX) : énumération PCI du virtio-gpu, initialisation du transport (réutilisé de Passerelle), files `controlq`/`cursorq`, gestion des ressources et du scanout, exposition en resource manager.
- **Display driver Screen** (`guest/display/`) : implémentation de l'interface display driver de Screen au-dessus du pilote virtio-gpu ; framebuffer, flip/blit, vsync (niveau C).
- **virtio-input minimal** (niveau C) : clavier + souris en invité, pour que le bureau soit utilisable (sinon affichage seul).
- **Volet B (niveau X)** : vdev virtio-gpu côté hôte QNX Hypervisor (si E4 concluant).
- **Jumeau Python** (`host/`) : modèle indépendant de la machine à commandes + différentiel C↔Python.
- **Codelab** : « Donner un affichage à une VM QNX ».

### 3.2 Hors périmètre (assumé)

3D accélérée (virgl/venus) — documentée en piste X, non implémentée ; pilotes de GPU physique ; passthrough PCI ; compositeur Wayland (idée distincte) ; diffusion d'écran vers navigateur (idée « Vitrine ») ; le transport virtio lui-même (fourni par Passerelle, Toile le consomme).

---

## 4. Architecture

```
toile/
├── CONTRACT.md                  # contrat gelé : commandes virtio-gpu acceptées, états, formats
├── TOILE-cahier-des-charges.md  # ce document
├── README.md
├── Makefile
├── LICENSE                      # MIT
├── core/                        # C11 PUR : ni horloge, ni fichier, ni socket, ni QNX
│   ├── include/toile/           #   vgproto.h resource.h scanout.h cmdproc.h verdict.h report.h
│   ├── src/                     #   vgproto.c resource.c scanout.c cmdproc.c verdict.c report.c
│   └── tests/                   #   test_cmdproc.c test_resource.c test_scanout.c test_verdict.c
├── host/                        # Python stdlib : jumeau indépendant + CLI + visualiseur
│   ├── toile/                   #   vgproto.py resource.py cmdproc.py replay.py cli.py report.py
│   └── tests/
├── guest/                       # code QNX invité (DDK, compilé avec qcc)
│   ├── virtio/                  #   lien/adaptation du transport virtio de Passerelle
│   ├── gpu/                     #   pilote virtio-gpu : files, ressources, scanout (resmgr /dev/gpu0)
│   ├── display/                 #   display driver Screen au-dessus du pilote
│   └── input/                   #   virtio-input minimal (niveau C)
├── host-qnx/                    # volet B (niveau X) : vdev virtio-gpu côté hôte QNX Hypervisor
├── qemu/                        # scripts de lancement QEMU (volet A) : -device virtio-gpu-pci, etc.
├── fixtures/                    # séquences de commandes virtio-gpu de référence (créées/validées sur PC)
├── scenarios/                   # cas d'usage : boot graphique, multi-scanout, resize, curseur
├── tests/                       # différentiel C↔Python, validation du modèle contre QEMU (trace)
├── codelab/                     # « donner un affichage à une VM QNX »
├── image/                       # buildfile mkifs pour une image QNX invitée graphique
└── docs/                        # SAFETY? (non critique) → DESIGN.md, GAP.md, DECISIONS.md, VALIDATION.md, VIRTIO-GPU.md
```

**Règle de portabilité.** `core/` ne lit aucun fichier, ne consulte aucune horloge, n'appelle aucune API QNX : l'appelant fournit les séquences de commandes et les tampons. Le même noyau s'exécute depuis un test unitaire sur PC, depuis le jumeau Python, et plus tard depuis le pilote invité. La double implémentation (C dans `core/`, Python dans `host/`) est un **jumeau de vérification** indépendant, comme sur Ambre, Passerelle, Tamis et Veille.

**Règle de réutilisation.** Le transport virtio (PCI moderne MSI-X, MMIO v1/v2) **n'est pas réécrit** : il est repris de Passerelle (même auteur, licence compatible MIT/Apache-2.0 à confirmer). Toile n'écrit que la couche virtio-gpu au-dessus. Zéro duplication (règle AGENTS.md).

**Règle de taille.** Tout fichier < 500 lignes, toute fonction < 50 lignes, vérifié en CI.

---

## 5. Contrat

### 5.1 Chaîne de traitement (figée)

```
commande virtio-gpu (guest) → parser → valider → appliquer à l'état (ressources/scanouts/backing) → effet hôte (flush/transfer) → acquittement
```

Aucune étape ne remonte. Toute commande invalide produit un verdict `TOILE_CMD_INVALID` avec l'identifiant de commande et la cause — jamais d'application partielle silencieuse.

### 5.2 État du modèle (ce que le cœur maintient)

- **Ressources** : table `resource_id → {format, width, height, backing attaché?, pages}`.
- **Scanouts** : table `scanout_id → {resource_id, x, y, width, height}`.
- **Backings** : pour chaque ressource, la liste des pages invité attachées.
- Invariants : un scanout ne peut référencer qu'une ressource existante ; `RESOURCE_FLUSH` n'est valide que sur une ressource attachée à un scanout ; `TRANSFER_TO_HOST_2D` exige un backing.

### 5.3 Verdicts (enum `toile_verdict_code`)

| # | Code | Sévérité | Porteur |
|---|---|---|---|
| 0 | `TOILE_OK` | info | cmd_id |
| 1 | `TOILE_CMD_INVALID` | error | cmd_id, cause |
| 2 | `TOILE_RESOURCE_EXISTS` | warning | resource_id |
| 3 | `TOILE_RESOURCE_MISSING` | error | resource_id, cmd_id |
| 4 | `TOILE_BACKING_MISSING` | error | resource_id, cmd_id |
| 5 | `TOILE_SCANOUT_INVALID` | error | scanout_id, resource_id |
| 6 | `TOILE_FLUSH_NO_SCANOUT` | warning | resource_id |
| 7 | `TOILE_TRANSFER_OK` | info | resource_id, rect |
| 8 | `TOILE_MODEL_CONSISTENT` | info | resources, scanouts |
| 9 | `TOILE_MODEL_COUNTEREXAMPLE` | error | cmd_id, état fautif |

### 5.4 API C (en-têtes sous `core/include/toile/`)

```c
/* cmdproc.h — applique une séquence de commandes à l'état du modèle */
int toile_cmdproc_run(const toile_cmd *cmds, size_t n,
                      toile_model *model,            /* état, fourni par l'appelant */
                      toile_verdict *out, size_t cap, size_t *n_out);

/* resource.h / scanout.h — accès à l'état */
int toile_resource_create(toile_model *m, uint32_t id, toile_fmt fmt, uint32_t w, uint32_t h);
int toile_resource_attach_backing(toile_model *m, uint32_t id, const toile_page *pages, size_t n);
int toile_scanout_set(toile_model *m, uint32_t scanout, uint32_t resource_id,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int toile_resource_flush(toile_model *m, uint32_t id, toile_rect rect);
```

### 5.5 Formats de sortie

Identiques en structure à ceux de Veille/Tamis (texte une ligne par verdict, JSON compact à ordre de clés figé, Markdown d'audit). Le contrat octet-pour-octet est précisé dans `CONTRACT.md` (à écrire, étape S1).

---

## 6. Exigences (S / C / X)

### 6.1 Socle (S) — le sous-ensemble livrable au 15/12

| ID | Exigence | Niveau |
|---|---|---|
| S1 | `CONTRACT.md` gelé : commandes virtio-gpu 2D acceptées, états du modèle, verdicts, formats | S |
| S2 | Cœur C11 pur : machine à commandes (create/attach/scanout/flush/transfer), invariants | S |
| S3 | Jumeau Python indépendant + différentiel C↔Python vert | S |
| S4 | Fixtures : séquences de commandes de référence (boot graphique, resize, multi-ressource) | S |
| S5 | Réutilisation du transport virtio de Passerelle (PCI moderne) — adapté, non réécrit | S |
| S6 | Driver invité minimal : énumération PCI virtio-gpu, init transport, controlq, scanout unique, flush | S |
| S7 | Display driver Screen minimal : un framebuffer, une tête, affichage d'une mire statique puis du bureau | S |
| S8 | Démo QEMU : image invitée QNX qui démarre Screen et affiche dans la fenêtre QEMU | S |
| S9 | `make test` vert **sans QNX, sans QEMU, sans GPU** (cœur + jumeau + fixtures) | S |
| S10 | CI : build C `-Werror`, tests, différentiel, garde taille, budget ≤ 5 min | S |

### 6.2 Complet (C) — la version visée à 3 mois

| ID | Exigence | Niveau |
|---|---|---|
| C1 | Multi-scanout : plusieurs têtes virtuelles, positionnement | C |
| C2 | Curseur matériel via `cursorq` | C |
| C3 | Suivi de dommages (damage tracking) : ne transférer que les rectangles modifiés | C |
| C4 | Double tampon (page flipping) + vsync | C |
| C5 | virtio-input minimal : clavier + souris en invité (rend le bureau utilisable) | C |
| C6 | Intégration Screen complète : fenêtres, applications Screen réelles lancées en invité | C |
| C7 | Resource manager `/dev/gpu0` avec API typée (ioctl/devctl) | C |
| C8 | Validation du modèle contre QEMU réel : capture des commandes émises par le driver, comparaison au modèle C | C |
| C9 | Codelab 4 chapitres : « donner un affichage à une VM QNX » | C |
| C10 | Image mkifs reproductible + scripts QEMU documentés | C |

### 6.3 Extrême (X) — documenté, hors concours

| ID | Exigence | Niveau |
|---|---|---|
| X1 | **Volet B** : vdev virtio-gpu côté hôte QNX Hypervisor (DDK hyperviseur, si E4 concluant) | X |
| X2 | Accélération 2D côté hôte (blits matériels, formats YUV) | X |
| X3 | Piste 3D : virgl (OpenGL invité) ou venus (Vulkan) — étude de faisabilité documentée | X |
| X4 | blob resources + host-visible memory (virtio-gpu moderne) pour performances | X |
| X5 | Contribution amont : patchs QNX au transport virtio, retour à Passerelle | X |

---

## 7. Méthode et preuve

### 7.1 Preuve par modèle (sans cible)

Le cœur est un **modèle exécutable** de la machine à commandes virtio-gpu. On le prouve sur des séquences de référence :
- **P1 (cohérence).** Après toute séquence valide, le modèle est cohérent : chaque scanout référence une ressource existante, chaque flush porte sur une ressource attachée. Verdict `TOILE_MODEL_CONSISTENT`.
- **P2 (rejet).** Toute commande invalide est rejetée avec un verdict précis, sans application partielle (ressource inexistante, flush sans scanout, transfer sans backing).
- **P3 (déterminisme).** La même séquence de commandes produit exactement le même état final et les mêmes verdicts, octet pour octet (hachage de l'état).

### 7.2 Validation contre QEMU réel (avec cible)

Sur une VM QEMU : le driver invité émet des commandes virtio-gpu. On les **capture** (instrumentation du driver ou trace côté QEMU) et on les **rejoue** dans le modèle C. Le modèle doit produire le même état que QEMU (mêmes ressources, mêmes scanouts). L'écart est un bug du modèle ou du driver. C'est le pendant « empirique » de la preuve par modèle — la même double preuve que Veille (modèle + trace).

### 7.3 Apport spécifique de QNX

| Mécanisme QNX | Rôle dans Toile |
|---|---|
| DDK + `libpci` | Énumération et pilotage du périphérique PCI virtio-gpu en invité |
| Resource managers | `/dev/gpu0` : un propriétaire, API typée, isolation |
| Microkernel + message passing | Le display driver Screen et le pilote GPU sont des processus séparés ; priority inheritance sur les flush critiques (pas de tearing sous charge) |
| Screen / `io-display` | L'interface display driver à implémenter |
| Adaptive partitioning | Le serveur Screen et les applis graphiques en partition best-effort, le pilote en partition critique |

---

## 8. Tests

| Test | But |
|---|---|
| Différentiel C↔Python | Même état final et mêmes verdicts sur les fixtures |
| Fixtures de commandes | Boot graphique, resize, multi-ressource, multi-scanout, séquences fautives |
| Invariants du modèle | P1/P2/P3 sur toutes les fixtures |
| Mutants | Injection de fautes dans `core/` pour vérifier que les tests les attrapent |
| Validation QEMU (C8) | Commandes réelles du driver == modèle C |
| Démo bout-en-bout (S8) | Image invitée qui démarre Screen dans QEMU |
| Garde taille | CI : fichiers < 500 lignes, fonctions < 50 lignes |

---

## 9. Démo et vidéo (< 3 min)

1. **(0:00–0:25)** Le problème : une VM QNX sous QEMU démarre, mais **aucun affichage** — console série seulement. Screen est inutilisable sans passthrough (G3).
2. **(0:25–1:00)** Toile : la même VM, avec le pilote virtio-gpu → **le bureau QNX s'affiche dans la fenêtre QEMU**. Une application Screen réelle est lancée.
3. **(1:00–1:40)** Le cœur portable : sur PC, sans QNX, `make test` rejoue des séquences de commandes virtio-gpu, prouve la cohérence du modèle (P1–P3), différentiel C↔Python vert.
4. **(1:40–2:20)** Validation croisée : les commandes émises par le driver réel dans QEMU sont capturées et rejouées dans le modèle C — même état. Double preuve.
5. **(2:20–3:00)** Niveau C : multi-écran virtuel, curseur, suivi de dommages (ne transférer que ce qui change). Codelab.

Visuel : fenêtre QEMU avec le bureau QNX + terminal hôte montrant les tests. Pas de musique sous copyright, pas de marque tierce.

---

## 10. À valider avant d'écrire la moindre ligne de produit

| Essai | Question | Bloquant |
|---|---|---|
| **E1** | Un invité QNX 8.0 sous QEMU (`-device virtio-gpu-pci`) **énumère-t-il** le périphérique sur son bus PCI (visible via le driver PCI / `pci` ou `libpci`) ? | oui (volet A) |
| **E2** | Screen accepte-t-il un **display driver tiers** via son interface documentée (quel est le contrat exact : `io-display`, bibliothèque de display driver, points d'entrée) ? | oui (S7) |
| **E3** | Le transport virtio de **Passerelle** est-il réutilisable tel quel pour virtio-gpu (files multiples, controlq/cursorq, MSI-X) ? licence compatible ? | oui (S5) |
| **E4** | L'accès au **DDK hyperviseur** QNX est-il permis sous licence non commerciale (QNX Everywhere) ? | oui pour **volet B seulement** (X1) |
| **E5** | Les performances d'un flush 2D via virtio-gpu (copie du framebuffer vers l'hôte) sont-elles suffisantes pour un bureau utilisable (≥ 15–20 images/s en 1080p) ? | non (ajuste les ambitions C) |

**E1 et E2 sont les deux essais critiques du volet A.** S'ils passent, le socle S est réalisable. E4 ne bloque que le volet B (X).

---

## 11. Planning (13 semaines, 12 nov → ~11 fév, avec jalon 15/12)

| Semaines | Livrable |
|---|---|
| S1 (12–18 nov) | E1, E2, E3 tranchés. `CONTRACT.md` gelé. Cœur C11 (machine à commandes) + fixtures. |
| S2 (19–25 nov) | Jumeau Python + différentiel vert. Modèle validé sur toutes les fixtures (P1–P3). |
| S3 (26 nov–2 déc) | Driver invité minimal : énumération PCI, transport Passerelle adapté, controlq, scanout unique. |
| S4 (3–9 déc) | Display driver Screen minimal. **Image invitée qui affiche dans QEMU.** |
| **Jalon 15/12** | **Sous-ensemble S livré et soumis** : bureau QNX basique en VM QEMU, cœur portable testé, CI verte, codelab amorcé. |
| S5–S7 (17 déc–6 janv) | Niveau C : multi-scanout, curseur, suivi de dommages, double tampon/vsync. |
| S8–S9 (7–20 janv) | virtio-input (clavier/souris), intégration Screen complète, applis réelles. |
| S10 (21–27 janv) | Validation contre QEMU réel (C8), capture/rejeu des commandes. |
| S11–S12 (28 janv–10 fév) | Volet B si E4 OK (vdev hôte QNX Hypervisor) **ou** piste 3D virgl documentée. |
| S13 (avant ~11 fév) | Codelab complet, docs (DESIGN, GAP, DECISIONS, VALIDATION, VIRTIO-GPU), polissage, marge. |

---

## 12. Risques

| Risque | Impact | Mitigation |
|---|---|---|
| E1 échoue : invité QNX n'énumère pas virtio-gpu sous QEMU | Volet A bloqué | Tester plusieurs variantes QEMU (`virtio-gpu-pci`, `virtio-gpu-device` MMIO, `virtio-vga`) ; vérifier le support PCI de l'invité QNX ; au pire, volet B ou repli Réplique/Mouleur |
| E2 échoue : Screen n'accepte pas de display driver tiers | Pas d'intégration Screen | Documenter l'interface réelle ; au pire, fournir un framebuffer `/dev/gpu0` utilisable par une appli de démo directe (sans Screen) et livrer le pilote seul |
| E4 : DDK hyperviseur inaccessible en licence non commerciale | Volet B (X) bloqué | Le volet A (QEMU) n'en a pas besoin ; B est un bonus X |
| Performances insufisantes (flush 2D trop lent) | Bureau inutilisable | Suivi de dommages (C3), double tampon (C4), blob resources (X4) ; mesurer tôt (E5) |
| Réutilisation du transport Passerelle impossible | Réécrire virtio (cher) | E3 tôt ; sinon réutiliser au moins la couche PCI/MMIO et réécrire les files virtio |
| Chevauchement avec Passerelle (même transport virtio) | Jugé « pas substantiellement différent » | Toile = virtio-gpu + Screen (graphique) ; Passerelle = virtio-fs/9P (fichiers). Périphériques, domaines et démos distincts. Le transport partagé est cité explicitement (prior art, comme Passerelle cite ses prédécesseurs). |
| 3 mois > deadline concours | Soumission partielle | Le sous-ensemble S (bureau basique en VM) est livrable au 15/12 ; C/X après |

---

## 13. Statut des affirmations

| Affirmation | Statut |
|---|---|
| virtio-gpu absent des ~20 vdev QNX Hypervisor (G1) | À revérifier page à page dans le SDP installé |
| Aucun display driver Screen virtio-gpu en invité (G2) | À revérifier |
| Screen inutilisable en invité sans passthrough (G3) | Vérifié dans l'inventaire lot D ; à confirmer sur QEMU (E1/E2) |
| QEMU fournit `virtio-gpu-pci` mature | Vrai (standard, utilisé par Linux/BSD) ; à confirmer côté invité QNX (E1) |
| Transport virtio réutilisable depuis Passerelle | À valider (E3) |
| DDK hyperviseur accessible en licence non commerciale | **Non vérifié** (E4) — bloque le volet B seulement |
| Interface display driver Screen | **Vérifié le 2026-11-12** : le contrat réel n'est **pas** `io-display` (absent du SDP 8.0) mais un **driver OpenWF Display (WFD)** couplé à la **bibliothèque Wfdcfg** — en-têtes `WF/wfd.h`, `WF/wfdext.h`, `wfdqnx/wfdcfg.h` du SDP 8.0, points d'entrée relus et squelette compilé (`qcc … -lscreen`). **Intégration non testée** : `libWFDplatform`, `wfd-server` et `libwfdcfg.so` sont hors du SDP libre (BSP « Screen Board Support package »). Voir `docs/TRIALS-E1-E4.md` §E2 et les décisions D17/D18 |
| Structures virtio-gpu de QEMU | À relire dans les sources QEMU (`hw/display/virtio-gpu.c`) |
| `TRANSFER_FROM_HOST_2D` existe (cité en §2.3) | **Corrigé le 2026-11-12** : inexistant en virtio 1.x — la lecture hôte → invité est `TRANSFER_FROM_HOST_3D` (**0x0206**), valable aussi pour une ressource créée en 2D ; Toile l'expose sous le nom `transfer_from_host` |
| `SET_CURSOR` (**0x0302**) existe (cité en §2.3) | **Corrigé le 2026-11-12** : inexistant — l'enum `virtio_gpu_ctrl_type` s'arrête à `MOVE_CURSOR` (**0x0301**) ; Toile n'implémente pas 0x0302 |
| `VIRTIO_GPU_MAX_SCANOUTS` = 16 | **Confirmé le 2026-11-12** dans l'en-tête standard `virtio_gpu.h` |
| Structures virtio-gpu relues dans l'en-tête standard | **Fait le 2026-11-12** (`virtio_gpu.h` Linux/QEMU) : codes de commandes, formats de pixels, limites et dispositions de structures repris dans `CONTRACT.md` §3 et §9. Reste la relecture côté hôte dans `hw/display/virtio-gpu.c` |

Mise à jour du **2026-11-12** : relecture de l'en-tête standard `virtio_gpu.h`
(`enum virtio_gpu_ctrl_type`, `enum virtio_gpu_formats`, `struct
virtio_gpu_ctrl_hdr`, `VIRTIO_GPU_MAX_SCANOUTS`). Les deux corrections ci-dessus
sont gravées dans `CONTRACT.md` §3 et §9, détaillées dans `docs/GAP.md` §3 et
`docs/VIRTIO-GPU.md` §4. Elles ne changent aucun autre statut. **E1** est
tranché (positif), **E2** partiellement tranché (interface WFD + Wfdcfg
identifiée, intégration non testée), **E4** tranché (négatif), **E5** : débit
du modèle mesuré (`docs/TRIALS-E5.md`), débit réel invité → hôte non mesuré.

Comme Tamis et Veille : toute correction d'affirmation est consignée ici.

---

## 14. Références

- Inventaire des vdev QNX Hypervisor (lot D du dossier `qnx-dossier`) — liste vérifiée des ~20 périphériques virtuels livrés.
- Spécification virtio (OASIS), section « GPU Device » : commandes 2D, files, blob resources.
- QEMU : `hw/display/virtio-gpu.c`, `-device virtio-gpu-pci`, `virtio-vga`.
- QNX SDP 8.0 : Screen Developer's Guide (interface display driver), DDK, `libpci`, Resource Manager Library, Hypervisor documentation (vdev, DDK hyperviseur).
- Projet frère **Passerelle** : transport virtio (PCI moderne MSI-X, MMIO v1/v2) pour QNX 8.0 — `github.com/BaptisteFaisy/passerelle`.
- Idées voisines du lot D : Main (virtio-input), Onde (virtio-snd), Ballon (virtio-balloon), Veine (vsock), Vitrine (diffusion d'écran), Compositeur (Wayland).
- QNX Global Hackathon 2026 : https://qnx.devpost.com/ — règles, catégories, bonus.

---

## 15. Décisions de conception

| # | Décision | Justification |
|---|---|---|
| D1 | Cœur C11 pur modélisant la machine à commandes, séparé du driver | Testable sans QNX/QEMU/GPU ; jumeau Python comparable ; le driver n'est que la colle |
| D2 | Réutiliser le transport virtio de Passerelle, ne pas le réécrire | Zéro duplication ; la brique la plus difficile est déjà validée |
| D3 | Deux volets A (QEMU) / B (QNX Hypervisor), A d'abord | A ne dépend pas du DDK hyperviseur (risque E4) ; A est livrable au 15/12 |
| D4 | Le sous-ensemble 15/12 = volet A minimal (scanout unique, mire puis bureau) | Tient dans 4 semaines ; démo spectaculaire (bureau QNX en VM) |
| D5 | 2D d'abord, 3D (virgl/venus) documentée en piste X | La 3D triple l'effort ; le besoin réel (Screen, IHM) est 2D |
| D6 | Validation du modèle contre QEMU réel (capture/rejeu des commandes) | Double preuve : modèle portable + comportement réel, comme Veille/Ambre |
| D7 | virtio-input en niveau C, pas S | Sans entrées, l'affichage seul démontre déjà le trou comblé ; les entrées rendent le bureau utilisable |
| D8 | MIT, cohérent avec le portefeuille | — |
| D9 | Codelab inclus | Bonus Best Codelab/Tutorial ; pédagogie « VM QNX graphique » forte |

---

## 16. Lots délégables (pour orchestrateur)

| Lot | Contenu | Dépend de |
|---|---|---|
| L1 | Cœur C11 : machine à commandes + invariants + verdicts | CONTRAT gelé |
| L2 | Jumeau Python + différentiel | L1 |
| L3 | Fixtures + scénarios (séquences de commandes de référence) | L1 |
| L4 | Driver invité : énumération PCI + transport Passerelle adapté | E1, E3 |
| L5 | Driver invité : controlq, ressources, scanout, flush | L4 |
| L6 | Display driver Screen minimal | E2, L5 |
| L7 | Image mkifs + scripts QEMU + démo bout-en-bout | L6 |
| L8 | virtio-input (clavier/souris) | L5 |
| L9 | Multi-scanout, curseur, dommages, double tampon | L6 |
| L10 | Validation contre QEMU réel (capture/rejeu) | L5, L6 |
| L11 | Volet B : vdev hôte QNX Hypervisor | E4 |
| L12 | Codelab + docs | L1–L7 |

---

*Fin du cahier des charges — Toile v0.1.*
