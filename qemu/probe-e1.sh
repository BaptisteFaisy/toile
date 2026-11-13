#!/usr/bin/env bash
# Toile — sonde E1 : un invité QNX 8.0 sous QEMU énumère-t-il le
# périphérique virtio-gpu sur son bus PCI ?
#
# Procédure (identique à celle qui a produit le verdict, 2026-11-12) :
#   1. construction d'une image IFS minimale (image/toile-e1.build) avec
#      mkifs du SDP QNX 8.0 — noyau + serveur PCI + pci-tool + shell ksh
#      sur la ligne série ;
#   2. démarrage d'une VM QEMU avec le périphérique virtio choisi
#      (-device virtio-gpu-pci par défaut) ;
#   3. capture de la console série jusqu'aux marqueurs
#      "=== TOILE_E1_PROBE_BEGIN ===" / "=== TOILE_E1_PROBE_END ===" ;
#   4. verdict : le couple 1af4/1050 (virtio-gpu moderne) doit apparaître
#      dans la sortie de pci-tool.
#
# Variables d'environnement :
#   TOILE_SDP        racine du SDP          (défaut : ~/qnx800)
#   TOILE_BUILDFILE  buildfile de la sonde  (défaut : <repo>/image/toile-e1.build)
#   TOILE_WORK       répertoire de travail  (défaut : /tmp/toile-e1)
#   TOILE_VARIANT    périphérique QEMU      (défaut : virtio-gpu-pci)
#   TOILE_MEM        mémoire Mo             (défaut : 1536)
#   TOILE_TIMEOUT    secondes d'attente     (défaut : 90)
#   TOILE_KEEP_VM    1 = ne pas arrêter la VM à la fin
#
# Code de sortie : 0 si le périphérique est énuméré, 1 sinon.

set -u

TOILE_SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TOILE_REPO_DIR=$(cd -- "${TOILE_SCRIPT_DIR}/.." && pwd)

TOILE_SDP=${TOILE_SDP:-"$HOME/qnx800"}
TOILE_BUILDFILE=${TOILE_BUILDFILE:-"${TOILE_REPO_DIR}/image/toile-e1.build"}
TOILE_WORK=${TOILE_WORK:-/tmp/toile-e1}
TOILE_VARIANT=${TOILE_VARIANT:-virtio-gpu-pci}
TOILE_MEM=${TOILE_MEM:-1536}
TOILE_TIMEOUT=${TOILE_TIMEOUT:-90}
TOILE_KEEP_VM=${TOILE_KEEP_VM:-0}

IFS_IMAGE="${TOILE_WORK}/toile-e1.ifs"
SERIAL_LOG="${TOILE_WORK}/serial-${TOILE_VARIANT}.log"
QEMU_PID_FILE="${TOILE_WORK}/qemu-${TOILE_VARIANT}.pid"

log() { printf '[probe-e1] %s\n' "$*" >&2; }
die() { log "ERREUR: $*"; exit 2; }

command -v qemu-system-x86_64 >/dev/null 2>&1 || die "qemu-system-x86_64 absent"
[ -f "${TOILE_BUILDFILE}" ] || die "buildfile absent: ${TOILE_BUILDFILE}"
mkdir -p "${TOILE_WORK}" || die "création de ${TOILE_WORK} impossible"

# --- 1. construction de l'image ------------------------------------------
if [ ! -f "${IFS_IMAGE}" ] || [ "${TOILE_BUILDFILE}" -nt "${IFS_IMAGE}" ]; then
    log "construction de l'IFS avec mkifs (SDP ${TOILE_SDP})"
    # shellcheck disable=SC1091
    . "${TOILE_SDP}/qnxsdp-env.sh" >/dev/null 2>&1 || die "qnxsdp-env.sh introuvable"
    command -v mkifs >/dev/null 2>&1 || die "mkifs absent (sourcez qnxsdp-env.sh)"
    ( cd "${TOILE_REPO_DIR}" && mkifs "${TOILE_BUILDFILE}" "${IFS_IMAGE}" ) \
        || die "mkifs a échoué"
fi
log "image: ${IFS_IMAGE} ($(wc -c < "${IFS_IMAGE}") octets)"

# --- 2. lancement de la VM -------------------------------------------------
QEMU_ARGS=(
    -m "${TOILE_MEM}"
    -kernel "${IFS_IMAGE}"
    -vga none
    -device "${TOILE_VARIANT}"
    -display none
    -serial "file:${SERIAL_LOG}"
    -monitor none
    -pidfile "${QEMU_PID_FILE}"
)
if [ -w /dev/kvm ]; then
    QEMU_ARGS+=(-enable-kvm -cpu host)
    log "KVM utilisé"
else
    QEMU_ARGS+=(-cpu max)
    log "KVM indisponible : émulation logicielle (plus lente)"
fi

rm -f "${SERIAL_LOG}"
log "démarrage : qemu-system-x86_64 ${QEMU_ARGS[*]}"
qemu-system-x86_64 "${QEMU_ARGS[@]}" >"${TOILE_WORK}/qemu-${TOILE_VARIANT}.log" 2>&1 &
QEMU_PID=$!
cleanup() {
    if [ "${TOILE_KEEP_VM}" != "1" ] && kill -0 "${QEMU_PID}" 2>/dev/null; then
        kill "${QEMU_PID}" 2>/dev/null
    fi
}
trap cleanup EXIT

# --- 3. attente de la fin de la sonde -------------------------------------
log "attente de la sortie série (max ${TOILE_TIMEOUT}s)"
DEADLINE=$(( $(date +%s) + TOILE_TIMEOUT ))
while [ "$(date +%s)" -lt "${DEADLINE}" ]; do
    if [ -f "${SERIAL_LOG}" ] && grep -q 'TOILE_E1_PROBE_END' "${SERIAL_LOG}"; then
        break
    fi
    sleep 2
done

# --- 4. verdict ------------------------------------------------------------
log "----- console série capturée -----"
if [ -f "${SERIAL_LOG}" ]; then
    sed 's/\r$//' "${SERIAL_LOG}" >&2
else
    log "pas de log série"
fi

GPU_LINE=$(grep -E '1af4/1050' "${SERIAL_LOG}" 2>/dev/null | head -1)
if [ -n "${GPU_LINE}" ]; then
    log "VERDICT E1 = POSITIF : périphérique virtio-gpu (1af4/1050) énuméré"
    log "  ${GPU_LINE}"
    exit 0
fi
log "VERDICT E1 = NÉGATIF : aucun 1af4/1050 dans la sortie de pci-tool"
exit 1
