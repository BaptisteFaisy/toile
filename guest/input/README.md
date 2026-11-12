# guest/input — virtio-input minimal (clavier + souris)

**Statut : squelette fonctionnel, NON livré au 15/12** (niveau C, exigence
C5 ; décision D7 du cahier des charges : « virtio-input en niveau C, pas S :
sans entrées, l'affichage seul démontre déjà le trou comblé »).

## Ce qui est fait

`toile_input.c` :

- ouvre le périphérique virtio-input (type **18**, DID PCI **0x1052**) via la
  couche transport de Toile (`guest/virtio/`), donc via Passerelle ;
- négocie les caractéristiques et ouvre la file `eventq` (index 0) ;
- pousse des tampons vides de 8 octets que le périphérique remplit avec des
  `struct virtio_input_event` (`le16 type`, `le16 code`, `le32 value`,
  spécification OASIS virtio 1.x §5.8) ;
- décode et affiche les événements (`syn`, `key`, `rel`, `abs`).

Le type 18 a été ajouté aux listes blanches de Passerelle par
`guest/virtio/toile-virtio-gpu.patch` (mêmes emplacements que le type 16).

## Ce qui manque (assumé)

| Manque | Conséquence |
|---|---|
| Injection des événements dans Screen | Le clavier et la souris ne pilotent pas encore le bureau : il faut soit `screen_inject_event()` (`screen/screen.h:7969`, contexte `SCREEN_INPUT_PROVIDER_CONTEXT`, privilèges à vérifier), soit un périphérique d'entrée QNX standard (`/dev/kbd*`, `/dev/ser*`) créé par le pilote |
| Lecture du nom et des capacités du périphérique | Pas de `VIRTIO_INPUT_CFG_ID_NAME` / `CFG_UNIQ` : le squelette n'interroge pas l'espace de configuration |
| File `statusq` (index 1) | Non utilisée (acknowledgements d'événements) |
| Gestion des codes clavier | Pas de table de traduction (les codes sont ceux de Linux `input-event-codes`) |

## Compilation

```sh
source ~/qnx800/qnxsdp-env.sh
make -f common.mk input      # -> build/toile-input
```

Compilé et lié le 2026-11-12 (52 760 octets, `qcc -V gcc_ntox86_64
-std=gnu11 -Wall -Wextra -Werror`, 0 avertissement).  **Non exécuté sur
cible** : il faut une VM QEMU avec `-device virtio-input-host-pci` (ou
`virtio-keyboard-pci` / `virtio-mouse-pci`), ce qui n'a pas encore été testé.
