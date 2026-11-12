#!/usr/bin/env bash
# Toile — lance une VM QEMU invitée avec le périphérique virtio-gpu.
#
# Variables d'environnement :
#   TOILE_IFS        image IFS à démarrer (défaut : /tmp/toile.ifs)
#   TOILE_BUILD      buildfile à compiler si l'image manque (défaut : <repo>/image/toile.build)
#   TOILE_GPU        périphérique QEMU (défaut : virtio-gpu-pci)
#   TOILE_MEM        mémoire Mo (défaut : 2048)
#   TOILE_SMP        cœurs (défaut : 4)
#   TOILE_DISK       disque optionnel (raw/vmdk) attaché en IDE
#   TOILE_SERIAL_LOG fichier de capture série (défaut : /tmp/toile-serial.log)
#   TOILE_SERIAL_SOCK socket série (défaut : /tmp/toile-serial.sock) ; vide = stdio
#   TOILE_DISPLAY    valeur de -display (défaut : none)
#   TOILE_KVM        1 = forcer KVM, 0 = sans KVM (défaut : auto)
#   TOILE_EXTRA      arguments QEMU additionnels
#
# Exemples :
#   ./run-toile.sh                       # boot de l'image graphique, série dans /tmp
#   TOILE_GPU=virtio-vga ./run-toile.sh  # variante VGA
#   TOILE_SERIAL_SOCK= ./run-toile.sh    # série sur le terminal

set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)

TOILE_IFS=${TOILE_IFS:-/tmp/toile.ifs}
TOILE_BUILD=${TOILE_BUILD:-"${REPO_DIR}/image/toile.build"}
TOILE_GPU=${TOILE_GPU:-virtio-gpu-pci}
TOILE_MEM=${TOILE_MEM:-2048}
TOILE_SMP=${TOILE_SMP:-4}
TOILE_DISK=${TOILE_DISK:-}
TOILE_SERIAL_LOG=${TOILE_SERIAL_LOG:-/tmp/toile-serial.log}
TOILE_SERIAL_SOCK=${TOILE_SERIAL_SOCK:-/tmp/toile-serial.sock}
TOILE_DISPLAY=${TOILE_DISPLAY:-none}
TOILE_KVM=${TOILE_KVM:-auto}
TOILE_EXTRA=${TOILE_EXTRA:-}

log() { printf '[run-toile] %s\n' "$*" >&2; }
die() { log "ERREUR: $*"; exit 2; }

command -v qemu-system-x86_64 >/dev/null 2>&1 || die "qemu-system-x86_64 absent"

# --- construction de l'image si nécessaire ---------------------------------
if [ ! -f "${TOILE_IFS}" ]; then
    log "image absente : construction avec mkifs (${TOILE_BUILD})"
    # shellcheck disable=SC1091
    . "${QNX_SDP:-$HOME/qnx800}/qnxsdp-env.sh" >/dev/null 2>&1 \
        || die "qnxsdp-env.sh introuvable (QNX_SDP=...)"
    command -v mkifs >/dev/null 2>&1 || die "mkifs absent"
    ( cd "${REPO_DIR}" && make -f guest/common.mk ) || die "build invité échoué"
    ( cd "${REPO_DIR}" && mkifs "${TOILE_BUILD}" "${TOILE_IFS}" ) \
        || die "mkifs a échoué"
fi
log "image : ${TOILE_IFS} ($(wc -c < "${TOILE_IFS}") octets)"

# --- ligne de commande QEMU --------------------------------------------------
QEMU_ARGS=(
    -m "${TOILE_MEM}"
    -smp "${TOILE_SMP}"
    -kernel "${TOILE_IFS}"
    -vga none
    -device "${TOILE_GPU}"
    -display "${TOILE_DISPLAY}"
    -monitor none
)
if [ -n "${TOILE_DISK}" ]; then
    QEMU_ARGS+=(-drive "file=${TOILE_DISK},if=ide,id=drv0")
fi
if [ -n "${TOILE_SERIAL_SOCK}" ]; then
    rm -f "${TOILE_SERIAL_SOCK}"
    QEMU_ARGS+=(-chardev "socket,id=ser0,path=${TOILE_SERIAL_SOCK},server=on,wait=off"
                -serial chardev:ser0)
    log "console série : socket ${TOILE_SERIAL_SOCK} (socat - UNIX-CONNECT:${TOILE_SERIAL_SOCK})"
else
    QEMU_ARGS+=(-serial mon:stdio)
fi
if [ "${TOILE_KVM}" = "1" ] || { [ "${TOILE_KVM}" = "auto" ] && [ -w /dev/kvm ]; }; then
    QEMU_ARGS+=(-enable-kvm -cpu host)
    log "KVM activé"
else
    QEMU_ARGS+=(-cpu max)
    log "KVM indisponible : émulation logicielle"
fi
if [ -n "${TOILE_EXTRA}" ]; then
    # shellcheck disable=SC2206
    QEMU_ARGS+=(${TOILE_EXTRA})
fi

log "qemu-system-x86_64 ${QEMU_ARGS[*]}"
exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
