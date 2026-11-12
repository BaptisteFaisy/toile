# guest/virtio — couche transport virtio de Toile (adaptation de Passerelle)

**Décision D2 du cahier des charges : le transport virtio n'est PAS réécrit.**
Ce dossier contient une **couche mince** (`toile_transport.h` /
`toile_transport.c`) au-dessus de l'API publique `pgw_vio_*` / `pgw_vq_*` du
projet frère **Passerelle** (même auteur, licence **Apache-2.0**), plus le
patch minimal qui ouvre la découverte PCI/MMIO de Passerelle au type de
périphérique virtio-gpu.

## Fichiers

| Fichier | Rôle |
|---|---|
| `toile_transport.h` | API Toile du transport : ouverture PCI/mock, files `controlq`/`cursorq`, soumission synchrone et asynchrone, lecture de l'espace de configuration |
| `toile_transport.c` | Implémentation : délégation intégrale à Passerelle (`pgw_vio_pci_open`, `pgw_vq_create/push/kick/pop`, `pgw_vio_init_device`, hook `dev->vq_alloc` pour la mémoire DMA) |
| `toile-virtio-gpu.patch` | Patch amont minimal (Apache-2.0, dérivé de Passerelle) ajoutant le type GPU 16 / DID 0x1050 aux trois points de découverte |

## Pointer le build vers Passerelle

Le build invité (`guest/common.mk`) ne contient **aucune copie** du code
Passerelle. Il travaille à partir de la variable `PASSERELLE_DIR` :

```sh
# valeur par défaut (cette machine) :
make -f common.mk PASSERELLE_DIR=/home/baptiste/Bureau/passerelle
```

Mécanique (voir `guest/common.mk`) :

1. les sources `$(PASSERELLE_DIR)/virtio/` et `$(PASSERELLE_DIR)/common/include/`
   sont **copiées** dans `guest/build/pgw/` (l'arbre partagé Passerelle n'est
   jamais modifié) ;
2. `toile-virtio-gpu.patch` y est appliqué (`patch -p1`) ;
3. le tout est compilé avec `qcc -V gcc_ntox86_64`.

## Essai E3 — le transport Passerelle est-il réutilisable tel quel ?

**Verdict : réutilisable, sous réserve d'un patch amont de 15 lignes
(fourni ici). Détail et preuves dans `docs/TRIALS-E1-E4.md` §E3.**

Ce qui est réutilisable **sans aucune modification** (vérifié dans le code,
2026-11-12) :

- **files multiples** : `pgw_vq_create(pgw_vio_dev *dev, uint16_t qidx, …)`
  est générique en `qidx` ; `dev->info.num_queues` vient du registre
  `num_queues` du périphérique (`pci_modern.c:465`). `controlq` (0) et
  `cursorq` (1) fonctionnent donc tels quels ;
- **MSI-X** : `pgw_vio_pci_open(…, bool allow_msix)` + programmation dans
  `pgw_vio_pci_sim_open` (`pci_modern.c`, `pci_msix_program`), état exposé
  via `pgw_vio_info.msix_active` ;
- **soumission scatter-gather avec adresses physiques** :
  `pgw_vq_push(vq, const pgw_vio_sg *sg, size_t count, void *cookie)`
  (`virtq.c:80` utilise `sg->phys` quand il est non nul) ;
- **mémoire DMA QNX** : hook `dev->vq_alloc` = `pgw_qnx_vq_alloc`
  (`qnx_mem.c`, `mmap` `MAP_PHYS|PROT_NOCACHE` + `mem_offset64`) — la couche
  Toile l'utilise pour ses tampons de commandes/réponses, sans `#ifdef` ;
- **négociation/spec** : `pgw_vio_init_device(dev, features)` (statut
  ACK→DRIVER→FEATURES_OK→DRIVER_OK, §3.1).

Ce qui **bloque** sans patch (les trois points de découverte sont codés en
dur pour 9P/virtiofs) :

| Emplacement | Blocage |
|---|---|
| `virtio/src/pci_qnx.c:169` `pci_device_id_for()` | ne traduit que les types 9 (9P) et 26 (virtiofs) en DID PCI ; type 16 → `0` → `pgw_vio_pci_open` renvoie NULL |
| `virtio/src/pci_modern.c:393` `pci_id_matches()` | ne reconnaît que les DID 0x1009/0x1049/0x105A ; 0x1050 (GPU) rejeté |
| `virtio/src/pci_modern.c:403` `pci_match_tag()` et `virtio/src/mmio.c:244` `match_tag()` | lisent **inconditionnellement** un « mount tag » 9P/virtiofs dans l'espace de configuration spécifique — sans sens pour un GPU ; `pgw_vio_read_tag_fs` échoue (validation `num_request_queues`), donc l'ouverture échoue même avec `expected_tag = NULL` |
| `virtio/src/mmio.c:283` | `pgw_vio_mmio_open` rejette tout `DeviceID` ≠ 9/26 (variante MMIO de E1) |

Le patch `toile-virtio-gpu.patch` corrige exactement ces quatre points
(ajout des constantes `PGW_VIRTIO_ID_GPU` / `PGW_VIRTIO_PCI_DID_GPU_MODERN`
dans `common/include/pgw/limits.h`). C'est un **candidat naturel à la
contribution amont X5** (retour à Passerelle) : il ne change aucun
comportement existant, il élargit les listes blanches et court-circuite la
lecture du tag pour les périphériques qui n'en ont pas.

## Licence et attribution

- Passerelle : Apache-2.0 (voir `$(PASSERELLE_DIR)/LICENSE` et `NOTICE`).
- `toile-virtio-gpu.patch` est une œuvre dérivée de Passerelle : en-tête
  SPDX Apache-2.0 conservé, attribution dans le `NOTICE` à la racine de
  Toile.
- `toile_transport.h/.c` (code Toile original) : MIT, comme le reste du
  dépôt. La combinaison MIT + Apache-2.0 est licite (licences permissives) ;
  les obligations Apache-2.0 (notice, état des modifications) sont remplies
  par le `NOTICE` racine et l'en-tête du patch.
