# Chapitre 3 — Le pilote invité : virtio-gpu côté QNX

**Durée estimée : ~1 h 30**

## Objectif

Comprendre et faire tourner le pilote virtio-gpu **dans l'invité QNX** :
énumération PCI du périphérique, réutilisation du transport virtio du projet
frère Passerelle (adapté, non réécrit — Apache-2.0), initialisation des files
`controlq`/`cursorq`, émission des commandes du contrat, exposition en
resource manager `/dev/gpu0`, compilation avec `qcc` — et trancher l'essai
**E1**, la question critique du volet A.

## Prérequis

- Chapitres 1 et 2 lus (cycle de vie d'une ressource ; commandes du contrat).
- **SDP QNX 8.0** installé (programme QNX Everywhere, gratuit en usage non
  commercial) ; par convention ce chapitre suppose `~/qnx800/`.
- QEMU côté hôte, avec `virtio-gpu-pci` (vérifié au chapitre 1).
- Les sources du projet frère **Passerelle** (transport virtio QNX 8.0),
  pointées par la variable `PASSERELLE_DIR` au moment du build.

> **⚠ Honnêteté — statut de ce chapitre.** À la date d'écriture, l'essai
> **E1** (un invité QNX sous QEMU énumère-t-il le périphérique
> `virtio-gpu-pci` ?) n'est **pas encore tranché sur cible** : aucune des
> étapes exécutées *dans QNX* n'a été vérifiée. Chaque commande côté invité
> porte la mention **« à valider sur cible »**. Le journal des essais
> (`docs/TRIALS-E1-E4.md`, tenu par le lot « code invité ») fait foi ; ce
> chapitre sera mis à jour avec ses verdicts. Ce qui est **réel dans le dépôt**
> est signalé comme tel : l'interface du transport (`guest/virtio/toile_transport.h`,
> 129 lignes, présente) et le patch d'extension Passerelle
> (`guest/virtio/toile-virtio-gpu.patch`, 90 lignes, présent).

---

## Étape 1 — Ce qui existe déjà : le transport adapté de Passerelle

Toile **ne réécrit pas virtio**. La brique difficile — PCI moderne MSI-X,
MMIO v1/v2, files virtuelles, négociation — est déjà écrite et testée pour
QNX 8.0 dans Passerelle (même auteur, licence **Apache-2.0**, compatible avec
le MIT du projet ; attribution dans `NOTICE`). Toile n'ajoute que la couche
spécifique virtio-gpu. C'est l'essai **E3** du cahier des charges (§10) :
« le transport est-il réutilisable tel quel ? licence compatible ? ».

Concrètement, dans le dépôt :

- **`guest/virtio/toile_transport.h`** *(présent)* — l'interface de la couche
  d'adaptation. Chaque opération d'anneau, de négociation et d'interruption
  délègue à l'API `pgw_vio_*` / `pgw_vq_*` de Passerelle. Ce que la couche
  ajoute : l'identité virtio-gpu (type de périphérique **16**, DID PCI moderne
  **0x1050** = 0x1040 + 16), les files nommées `controlq` (0) / `cursorq` (1)
  du standard OASIS virtio 1.x §5.7, un pool de requêtes avec tampons DMA
  (commande lisible / réponse écrivable), un submit synchrone et un trio
  push/kick/poll asynchrone — et une ouverture **mock** qui rend la couche
  testable sur PC, sans QNX.
- **`guest/virtio/toile-virtio-gpu.patch`** *(présent)* — patch minimal
  (Apache-2.0) qui étend la découverte Passerelle au type GPU : les points
  d'entrée PCI/MMIO de Passerelle ne listaient que 9P/virtiofs. Le patch est
  appliqué par `guest/common.mk` à une **copie de build** des sources
  Passerelle — l'arbre partagé n'est **jamais** modifié.

## Étape 2 — Énumération PCI : l'essai E1

**La question (cahier des charges §10).** Un invité QNX 8.0 démarré sous QEMU
avec `-device virtio-gpu-pci` **énumère-t-il** le périphérique sur son bus PCI
— visible via le driver PCI / l'outil `pci` ou `libpci` ? C'est **bloquant
pour tout le volet A** : si QNX ne voit pas le périphérique, rien ne suit.

Côté hôte, le périphérique est bien là (chapitre 1, étape 3 — *vérifié sur la
machine de l'auteur*) : bus 0, device 2, `Display controller: PCI device
1af4:1050`. Côté invité, la procédure *(à valider sur cible)* :

1. Démarrer la VM QNX avec le périphérique — via le script `qemu/probe-e1.sh`
   (lot « scripts QEMU », à venir) ou la commande du chapitre 1, étape 6.
2. Dans le shell invité, lister le bus PCI avec l'outil `pci` du SDP
   *(à valider sur cible : nom exact de l'utilitaire et sortie à confirmer
   dans le SDP installé — section 13 du cahier des charges)* :

   ```
   # pci -v          # à valider sur cible
   ```

   Attendu si E1 passe : une entrée « Display controller », vendeur `1af4`,
   périphérique `1050`.
3. Alternative programmatique : un mini-sondeur `libpci` (quelques dizaines de
   lignes, `pci_attach` + recherche vendeur/périphérique) compilé avec `qcc`
   (étape 7). Le dépôt livrera cette sonde avec `qemu/probe-e1.sh`.
4. **Consigner le verdict** — sortie brute, version du SDP, ligne QEMU exacte
   — dans `docs/TRIALS-E1-E4.md`. E1 se tranche en jours, pas en semaines
   (planning S1, cahier des charges §11).

## Étape 3 — Ouvrir le périphérique depuis le pilote

Interface réelle de `guest/virtio/toile_transport.h` *(présente dans le
dépôt ; l'exécution sur cible est à valider)* :

```c
#include "toile_transport.h"

/* QNX : sonde le bus PCI (colle libpci de Passerelle) et ouvre le premier
 * périphérique virtio-gpu. NULL si absent (journalisé). */
toile_transport *tr = toile_tr_open_pci(/*allow_msix=*/true);

/* Négocie les fonctionnalités et passe le périphérique en DRIVER_OK.
 * VERSION_1 (transport moderne) est ajouté automatiquement. */
int rc = toile_tr_init(tr, /*driver_features=*/0);

/* Ouvre les files virtuelles : 0 = taille par défaut (64, plafonnée
 * par le périphérique). Indices gelés par le standard. */
rc = toile_tr_queue_open(tr, TOILE_VQ_CONTROLQ, 0);   /* file 0 : commandes */
rc = toile_tr_queue_open(tr, TOILE_VQ_CURSORQ,  0);   /* file 1 : curseur   */

/* Espace de configuration virtio-gpu : num_scanouts, events_read… */
struct virtio_gpu_config cfg;
rc = toile_tr_read_config(tr, 0, &cfg, sizeof cfg);
```

**Sur PC, sans QNX** — la même couche s'ouvre en mode mock, ce qui permet de
tester la logique du pilote hors cible (miroir de la pratique Passerelle) :

```c
toile_transport *tr = toile_tr_open_mock(TOILE_VQ_COUNT);
```

## Étape 4 — Émettre les commandes du contrat

Une commande = un tampon DMA **lisible** (la requête) apparié à un tampon
**écrivable** (la réponse, capacité `TOILE_TR_RESP_CAP` = 512 octets — assez
pour la réponse display-info de 408 octets, tout le reste tient dans
l'en-tête de contrôle de 24 octets). Deux modes :

```c
/* Synchrone : push + kick + attente + poll, avec timeout. */
rc = toile_tr_submit(tr, TOILE_VQ_CONTROLQ, &cmd, sizeof cmd,
                     &resp, sizeof resp, /*timeout_ms=*/1000);

/* Asynchrone : pool de TOILE_TR_POOL = 8 requêtes en vol. */
rc = toile_tr_push(tr, TOILE_VQ_CONTROLQ, &cmd, sizeof cmd,
                   &resp, sizeof resp, cookie);
rc = toile_tr_kick(tr, TOILE_VQ_CONTROLQ);
rc = toile_tr_poll(tr, TOILE_VQ_CONTROLQ, &cookie, &resp_len); /* EAGAIN si rien */
```

Les structures de commandes (`RESOURCE_CREATE_2D`, `SET_SCANOUT`, …) viennent
de `core/include/toile/vgproto.h` *(en-tête présent)* — dérivées des en-têtes
standard `virtio_gpu.h`, relues le 2026-11-12 (`NOTICE`). **C'est le même
cœur portable qu'au chapitre 2** : le pilote encode les commandes que le
modèle C sait rejouer. Conséquence méthodologique (exigence C8) : on pourra
capturer les commandes réellement émises dans QEMU et les rejouer dans le
modèle — mêmes verdicts, même hash d'état, double preuve.

Séquence d'affichage minimale dans le pilote *(à valider sur cible)* :
`get_display_info` → `resource_create_2d` (framebuffer) →
`resource_attach_backing` (pages du framebuffer) → `set_scanout` → boucle
`transfer_to_host_2d` + `resource_flush` à chaque dommage.

## Étape 5 — Exposer le périphérique : resource manager `/dev/gpu0`

Sur QNX, un pilote qui veut exister dans l'espace de noms s'écrit comme
**resource manager** (`resmgr_*`, cahier des charges §2.1). Le pilote Toile
s'attachera sur `/dev/gpu0` avec une API typée (ioctl/`devctl`) — exigence
**C7**. Rôle de cette façade :

- le **display driver Screen** (chapitre 4) ouvre `/dev/gpu0` et dialogue par
  `devctl` : allouer un framebuffer, définir le scanout, notifier un dommage,
  attendre un flush ;
- les **outils utilisateur** (sonde, banc de test, capture) peuvent interroger
  l'état du pilote sans privilège particulier ;
- le **noyau virtio-gpu portable** (chapitre 2) reste intégré au pilote : les
  commandes qu'il valide sont exactement celles qui partent sur `controlq`.

Code : `guest/gpu/` *(à écrire — semaines S3–S4 du planning)*. **À valider sur
cible** comme le reste du chapitre.

## Étape 6 — Compiler avec `qcc`

Environnement QNX 8.0 *(à valider sur cible : noms exacts à confirmer dans le
SDP installé)* :

```bash
$ source ~/qnx800/qnxsdp-env.sh          # charge l'environnement SDP
$ export PASSERELLE_DIR=/chemin/vers/passerelle
$ make -C guest                          # applique le patch à une COPIE, puis compile
```

Points durs :

- `qcc` cible l'invité QNX ; pour une VM QEMU x86-64, la variante de
  compilation est celle pour cible x86-64 QNX (`-V gcc_ntox86_64` **à
  confirmer** avec `qcc -V` dans le SDP installé).
- `guest/common.mk` applique `toile-virtio-gpu.patch` à une **copie de build**
  des sources Passerelle (`PASSERELLE_DIR`) — l'arbre partagé Passerelle n'est
  **jamais** modifié (règle du portefeuille, rappelée dans l'en-tête du patch).
- Toute fonction < 50 lignes, tout fichier < 500 lignes : la garde `taille.py`
  du chapitre 2 s'applique aussi au code invité.

## Étape 7 — L'essai E1, procédure complète et décision

**E1 est le premier travail réel du volet A** (planning S1). Procédure :

1. Hôte : vérifier `qemu-system-x86_64 -device help | grep virtio-gpu-pci`
   *(fait au chapitre 1 — vérifié)*.
2. Invité : démarrer la VM QNX 8.0 avec `-device virtio-gpu-pci` + console
   série *(à valider sur cible — commande exacte au chapitre 1, étape 6 ;
   script `qemu/probe-e1.sh` à venir)*.
3. Invité : énumérer le bus PCI (outil `pci` du SDP ou sondeur `libpci`) et
   chercher `1af4:1050` *(à valider sur cible)*.
4. Invité : lire l'espace de configuration du périphérique (BAR, capacités
   virtio modernes) — preuve qu'il est **utilisable**, pas seulement listé.
5. Consigner dans `docs/TRIALS-E1-E4.md` : ligne QEMU exacte, version SDP,
   sorties brutes, verdict.

| Verdict E1 | Conséquence |
|---|---|
| **PASS** — périphérique énuméré et configurable | Le socle S est réalisable : enchaîner S3 (pilote minimal) puis S4 (Screen) |
| **FAIL** — périphérique invisible | Journaliser ; pistes à explorer (hypothèses, pas des faits) : sous-système PCI QNX non démarré dans la configuration testée, variante MMIO `-device virtio-gpu-device`, options machine QEMU. Le volet A est **bloqué** tant que E1 n'est pas résolu |

> Rien dans ce chapitre ne doit être présenté comme « ça marche » avant le
> verdict PASS consigné dans `docs/TRIALS-E1-E4.md`.

## Exercices

1. **Transport mock sur PC** *(à valider selon livraison du lot « code
   invité »)* : écrire un petit programme qui ouvre `toile_tr_open_mock(2)`,
   initialise, ouvre les deux files et soumet un `get_display_info` encodé via
   `core/include/toile/vgproto.h`. Aucun QNX requis — la couche est conçue
   pour (voir l'en-tête).
2. **Lecture du patch** *(fichier présent)* : `guest/virtio/toile-virtio-gpu.patch`
   touche quatre fichiers de Passerelle. Les lister et expliquer chaque
   changement (indice : l'en-tête du patch les commente — limites d'identité,
   découverte PCI QNX, appariement PCI moderne, découverte MMIO).
3. **Capacités virtio modernes** *(hôte seul, vérifiable)* : avec la sonde du
   chapitre 1 (`info qtree`), retrouver les régions du périphérique virtio-gpu.
   Combien de BAR la sortie `info pci` montrait-elle ? (Deux : BAR1 mémoire
   32 bits, BAR4 mémoire 64 bits préfetchable.)
4. **Dimensionnement** : la réponse à `get_display_info` fait 24 + 16 × 24 =
   408 octets (en-tête + 16 structures `pmode`, une par tête possible).
   Expliquer pourquoi `TOILE_TR_RESP_CAP = 512` et pourquoi toutes les autres
   commandes tiennent dans 24 octets.

## Points de contrôle

- [ ] Énoncer l'essai E1, son statut bloquant pour le volet A, et où son verdict est consigné.
- [ ] Citer l'identité complète du périphérique : type virtio 16, DID PCI moderne 0x1050, files 0 (`controlq`) et 1 (`cursorq`).
- [ ] Expliquer le rapport Toile↔Passerelle : adaptation sous Apache-2.0, patch appliqué à une copie de build, jamais à l'arbre partagé.
- [ ] Différencier les modes synchrone (`submit`) et asynchrone (`push`/`kick`/`poll`) et la taille du pool (8 requêtes en vol).
- [ ] Dire pourquoi le cœur portable du chapitre 2 se retrouve **dans** le pilote, et ce que ça permettra (C8 : capture/rejeu, double preuve).

## Erreurs fréquentes

| Symptôme | Cause probable | Remède |
|---|---|---|
| `qcc: command not found` | environnement SDP non chargé | `source ~/qnx800/qnxsdp-env.sh` d'abord |
| Build invité échoue sur `pgw/virtio.h` introuvable | `PASSERELLE_DIR` non défini ou patch non appliqué à la copie | exporter `PASSERELLE_DIR` ; laisser `guest/common.mk` appliquer le patch |
| Envie de patcher l'arbre Passerelle partagé | raccourci tentant, interdit | le patch ne touche qu'une **copie de build** — règle du portefeuille |
| Périphérique introuvable dans l'invité | **E1 non tranché** : ne pas bricoler au-delà | consigner la sortie brute dans `docs/TRIALS-E1-E4.md`, trancher formellement |
| Confusion DID `0x1040` vs `0x1050` | virtio legacy vs moderne | moderne = 0x1040 + type de périphérique (16 = GPU) ; Toile vise le transport moderne (VERSION_1) |
| MSI-X indisponible dans la VM | configuration hôte ou invit | `toile_tr_open_pci(false)` : le transport retombe sur les interruptions classiques — prévu par l'interface |
| Réponse tronquée (`TOILE_TR_EIO`) | tampon réponse < capacité nécessaire | respecter `TOILE_TR_RESP_CAP` (512) ; ne pas redimensionner à la baisse |
