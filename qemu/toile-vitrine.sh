#!/usr/bin/env bash
# Toile — lancement PERSISTANT de l'invité QNX 8.0 pour la vitrine web.
#
# Différence avec qemu/probe-driver.sh : la sonde est un contrôle ponctuel (elle
# démarre la VM, attend la preuve, puis arrête la VM).  Ce script, lui, laisse
# la VM tourner : il sert à la vitrine HTTP (qnx-emu/web.py), qui lit l'écran
# par `screendump` sur le moniteur QEMU et la console par le journal série.
#
# La ligne QEMU est celle de la sonde (validée le 2026-11-13) :
#   - `-kernel <ifs>` : l'IFS se lance par le noyau, PAS par le disque ;
#   - `-vga none -device virtio-gpu-pci` : l'invité doit voir le
#     périphérique virtio-gpu (1af4/1050), c'est lui que Toile pilote ;
#   - `-vnc :N` : l'affichage du device virtio-gpu ; sans lui, `screendump`
#     ne rend pas la mire ;
#   - `-monitor unix:<sock>` : la commande HMP `screendump` ;
#   - `-serial file:<log>` : la console série, lue par la vitrine.
#
# Usage :
#   qemu/toile-vitrine.sh --ifs <ifs.bin> --serie <log> --moniteur <sock> \
#                          [--vnc 12] [--memoire 1536]
#
# Le script reste au premier plan (exec) : c'est systemd qui le supervise.
set -u

IFS_BIN=""
SERIE=""
MON=""
VNC_NUM=12
MEM=1536

while [ "$#" -gt 0 ]; do
    case "$1" in
        --ifs)      IFS_BIN="$2"; shift 2 ;;
        --serie)    SERIE="$2"; shift 2 ;;
        --moniteur) MON="$2"; shift 2 ;;
        --vnc)      VNC_NUM="$2"; shift 2 ;;
        --memoire)  MEM="$2"; shift 2 ;;
        --aide|-h)  sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)          echo "toile-vitrine: argument inconnu : $1" >&2; exit 2 ;;
    esac
done

[ -n "${IFS_BIN}" ] || { echo "toile-vitrine: --ifs obligatoire" >&2; exit 2; }
[ -f "${IFS_BIN}" ] || { echo "toile-vitrine: IFS introuvable : ${IFS_BIN}" >&2; exit 2; }
[ -n "${SERIE}" ] || { echo "toile-vitrine: --serie obligatoire" >&2; exit 2; }
[ -n "${MON}" ] || { echo "toile-vitrine: --moniteur obligatoire" >&2; exit 2; }

QEMU="${TOILE_QEMU:-$(command -v qemu-system-x86_64 || true)}"
[ -n "${QEMU}" ] || { echo "toile-vitrine: qemu-system-x86_64 introuvable" >&2; exit 2; }

mkdir -p "$(dirname "${SERIE}")" "$(dirname "${MON}")" || exit 2
rm -f "${MON}"

if [ -w /dev/kvm ]; then
    CPU_ARGS=(-enable-kvm -cpu host)
else
    CPU_ARGS=(-cpu max)
fi

echo "toile-vitrine: IFS=${IFS_BIN} serie=${SERIE} moniteur=${MON} vnc=:${VNC_NUM}"
exec "${QEMU}" \
    -m "${MEM}" \
    -kernel "${IFS_BIN}" \
    -vga none \
    -device virtio-gpu-pci \
    -vnc ":${VNC_NUM}" \
    -serial "file:${SERIE}" \
    -monitor "unix:${MON},server,nowait" \
    "${CPU_ARGS[@]}"
