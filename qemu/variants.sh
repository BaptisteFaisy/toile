#!/usr/bin/env bash
# Toile — les trois variantes de périphérique virtio-gpu à tester pour E1.
#
#   1. virtio-gpu-pci    : périphérique virtio-gpu moderne sur PCI (v1.0,
#                          device id 16 / DID 0x1050).  C'est la variante
#                          de référence pour QEMU x86_64.
#   2. virtio-gpu-device : transport virtio-MMIO (machine ARM « virt »),
#                          PAS de bus PCI : la variante correspondante
#                          côté invité est l'architecture aarch64, pas
#                          x86_64.  Sur cette machine (invité x86_64) elle
#                          n'est PAS applicable : elle sert de document de
#                          couverture pour la suite du projet.
#   3. virtio-vga        : périphérique virtio compatible VGA (même
#                          identifiant 1af4/1050, code de classe 03/00/00
#                          au lieu de 03/80/00) : le chemin VGA de QEMU.
#
# Usage :
#   ./variants.sh              # affiche les variantes et leurs commandes
#   ./variants.sh --probe      # lance la sonde E1 pour chaque variante
#   ./variants.sh --probe virtio-gpu-pci   # sonde une seule variante
#
# Code de sortie (mode --probe) : 0 si TOUTES les variantes sondées ont
# énuméré 1af4/1050, 1 sinon.

set -u

TOILE_SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROBE="${TOILE_SCRIPT_DIR}/probe-e1.sh"

VARIANTS=(virtio-gpu-pci virtio-gpu-device virtio-vga)

usage() {
    cat >&2 <<'EOF'
usage: variants.sh [--probe] [variante...]

Variantes :
  virtio-gpu-pci     PCI moderne, device id 16 / DID 0x1050 (référence)
  virtio-gpu-device  transport MMIO : architecture aarch64 uniquement
                     (non applicable à l'invité x86_64 de cette machine)
  virtio-vga         périphérique compatible VGA (même 1af4/1050)
EOF
}

show_variant() {
    local v=$1
    printf '\n=== %s ===\n' "${v}"
    case "${v}" in
    virtio-gpu-pci)
        printf 'QEMU : qemu-system-x86_64 --enable-kvm --cpu host -m 2048 \\\n'
        printf '         -kernel TOILE.ifs -vga none -device virtio-gpu-pci \\\n'
        printf '         -display none -serial file:sortie.log\n'
        printf 'Attendu : vid/did 1af4/1050, class/subclass 03/80/00\n'
        ;;
    virtio-gpu-device)
        printf 'QEMU : qemu-system-aarch64 -M virt -device virtio-gpu-device\n'
        printf '         (invité aarch64 ; l-invariant x86_64 n'\''a pas de MMIO)\n'
        printf 'Attendu : hors périmètre de cette machine (invité x86_64)\n'
        ;;
    virtio-vga)
        printf 'QEMU : qemu-system-x86_64 --enable-kvm --cpu host -m 2048 \\\n'
        printf '         -kernel TOILE.ifs -vga none -device virtio-vga \\\n'
        printf '         -display none -serial file:sortie.log\n'
        printf 'Attendu : vid/did 1af4/1050, class/subclass 03/00/00\n'
        ;;
    *)
        printf 'variante inconnue\n'
        return 1
        ;;
    esac
}

PROBE_MODE=0
SELECTED=()
for arg in "$@"; do
    case "${arg}" in
    --probe) PROBE_MODE=1 ;;
    --help|-h) usage; exit 0 ;;
    *) SELECTED+=("${arg}") ;;
    esac
done
if [ "${#SELECTED[@]}" -eq 0 ]; then
    SELECTED=("${VARIANTS[@]}")
fi

if [ "${PROBE_MODE}" -eq 0 ]; then
    for v in "${SELECTED[@]}"; do show_variant "${v}"; done
    exit 0
fi

rc=0
for v in "${SELECTED[@]}"; do
    show_variant "${v}"
    case "${v}" in
    virtio-gpu-device)
        printf '[variants] variante non applicable sur un invité x86_64 : ignorée\n'
        continue
        ;;
    esac
    if TOILE_VARIANT="${v}" "${PROBE}"; then
        printf '[variants] %s : OK (1af4/1050 énuméré)\n' "${v}"
    else
        printf '[variants] %s : ÉCHEC\n' "${v}"
        rc=1
    fi
done
exit "${rc}"
