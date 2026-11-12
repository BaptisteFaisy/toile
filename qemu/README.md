# qemu/ — scripts de lancement (volet A)

Trois scripts, testés le 2026-11-12 sur cette machine (QEMU 8.2.2,
`/usr/bin/qemu-system-x86_64`).

| Script | Rôle |
|---|---|
| `probe-e1.sh` | **procédure E1** : construit l'IFS de sonde, démarre la VM avec le périphérique choisi, capture la console série, tranche le verdict (présence de `1af4/1050`) |
| `run-toile.sh` | lance la VM QNX invitée avec le pilote Toile (image graphique ou image de sonde) |
| `variants.sh` | les trois variantes de périphérique à tester pour E1 (`virtio-gpu-pci`, `virtio-gpu-device` MMIO, `virtio-vga`) |

## Procédure E1, pas à pas

```sh
# 1. (une fois) licence QNX 8.0 + SDP installé
source ~/qnx800/qnxsdp-env.sh

# 2. sonde complète, verdict automatique
cd /home/baptiste/Bureau/toile
./qemu/probe-e1.sh          # -> "VERDICT E1 = POSITIF", code de sortie 0

# 3. les trois variantes
./qemu/variants.sh          # affiche les commandes
./qemu/variants.sh --probe  # sonde chaque variante applicable
```

### Ce que fait `probe-e1.sh`

1. **construction** : `mkifs image/toile-e1.build $TOILE_WORK/toile-e1.ifs`
   (image minimale : noyau + serveur PCI + `pci-tool` + shell `ksh` série) ;
2. **démarrage** :
   `qemu-system-x86_64 -m 1536 -kernel <ifs> -vga none -device <variante>
   -display none -serial file:<log> -monitor none` (+ `-enable-kvm -cpu host`
   si `/dev/kvm` est inscriptible, sinon `-cpu max`) ;
3. **capture** : le fichier série est lu jusqu'au marqueur
   `=== TOILE_E1_PROBE_END ===` (90 s maximum) ;
4. **verdict** : `grep '1af4/1050'` → code de sortie 0 = positif.

Variables : `TOILE_SDP`, `TOILE_BUILDFILE`, `TOILE_WORK`, `TOILE_VARIANT`,
`TOILE_MEM`, `TOILE_TIMEOUT`, `TOILE_KEEP_VM`.

## Résultat obtenu le 2026-11-12 (sortie brute)

```
=== TOILE_E1_PROBE_BEGIN ===
--- pci-tool ---

Partition: pci

Managing Server ID: 1, pid: 3, running
B000:D00:F00 @ idx 0
B000:D01:F00 @ idx 1
B000:D01:F01 @ idx 2
B000:D01:F03 @ idx 3
B000:D02:F00 @ idx 4
B000:D03:F00 @ idx 5

--- pci-tool -v ---

Partition: pci

Managing Server ID: 1, pid: 3, running
B000:D00:F00 @ idx 0
	vid/did: 8086/1237
...
B000:D02:F00 @ idx 4
	vid/did: 8086/100e
...
B000:D03:F00 @ idx 5
	vid/did: 1af4/1050
		<vendor id - unknown>, <device id - unknown>
	class/subclass/reg: 03/80/00
		<unavailable>

=== TOILE_E1_PROBE_END ===
[probe-e1] VERDICT E1 = POSITIF : périphérique virtio-gpu (1af4/1050) énuméré
```

Variante `virtio-vga` : même `1af4/1050`, mais `class/subclass/reg: 03/00/00`
(contrôleur VGA compatible) au lieu de `03/80/00` (contrôleur d'affichage).

## Lancer l'image de démonstration (pilote + mire)

```sh
cd /home/baptiste/Bureau/toile
./qemu/run-toile.sh                    # image /tmp/toile.ifs (construit si absent)
TOILE_GPU=virtio-vga ./qemu/run-toile.sh
TOILE_DISK=/chemin/disk.raw ./qemu/run-toile.sh     # avec un disque attaché
```

La console série est exposée sur un socket UNIX (`/tmp/toile-serial.sock`) :

```sh
socat - UNIX-CONNECT:/tmp/toile-serial.sock
```

## Limites connues (documentées)

- La variante `virtio-gpu-device` (transport MMIO) suppose un invité
  **aarch64** (machine `virt` de QEMU) : elle n'est pas applicable à l'invité
  x86_64 de cette machine, et le backend MMIO de Passerelle n'est de toute
  façon pas compatible QNX 8.0 (voir `docs/TRIALS-E1-E4.md` §E3).
- `-cpu max` (sans KVM) rend le démarrage très lent ; l'image QSTI
  « complète » (35 Go) demande un disque.
- Les images de ce dépôt sont **autonomes** (tout en RAM, via `-kernel`) :
  pas de `/system`, donc pas de bureau QNX complet — voir `image/README.md`.
