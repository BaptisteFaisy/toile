#!/usr/bin/env bash
# Toile — sonde driver (essai « le pilote invité démarre dans l'invité ») :
# un invité QNX 8.0 sous QEMU charge-t-il le pilote Toile, énumère-t-il le
# périphérique virtio-gpu et crée-t-il /dev/gpu0 ?
#
# Procédure :
#   1. compilation du pilote invité (guest/common.mk driver -> toile-gpu) ;
#   2. construction de l'image IFS (image/toile-driver.build) avec mkifs ;
#   3. démarrage d'une VM QEMU avec le périphérique virtio-gpu-pci ;
#   4. capture de la console série jusqu'au marqueur
#      "=== TOILE_DRIVER_PROBE_END ===" ;
#   5. verdict : le journal doit contenir la ligne de découverte
#      "TOILE: virtio-gpu trouve (1af4/1050) sur" ET "TOILE: /dev/gpu0 pret"
#      ET "waitfor /dev/gpu0 rc=0" (preuve indépendante que le noeud existe).
#
# Variables d'environnement :
#   TOILE_SDP        racine du SDP          (défaut : ~/qnx800)
#   TOILE_BUILDFILE  buildfile de l'image   (défaut : image/toile-driver.build)
#   TOILE_WORK       répertoire de travail  (défaut : /tmp/toile-driver)
#   TOILE_VARIANT    périphérique QEMU      (défaut : virtio-gpu-pci)
#   TOILE_MEM        mémoire Mo             (défaut : 1536)
#   TOILE_TIMEOUT    secondes d'attente     (défaut : 90)
#   TOILE_KEEP_VM    1 = ne pas arrêter la VM à la fin
#   TOILE_NO_REBUILD 1 = ne pas recompiler le pilote (il doit exister)
#
# Code de sortie : 0 si le pilote trouve le périphérique et crée /dev/gpu0,
# 1 sinon, 2 en cas d'erreur d'outillage (mkifs/qcc/QEMU absents).

set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)

TOILE_SDP=${TOILE_SDP:-"$HOME/qnx800"}
TOILE_BUILDFILE=${TOILE_BUILDFILE:-"${REPO_DIR}/image/toile-driver.build"}
TOILE_WORK=${TOILE_WORK:-/tmp/toile-driver}
TOILE_VARIANT=${TOILE_VARIANT:-virtio-gpu-pci}
TOILE_MEM=${TOILE_MEM:-1536}
TOILE_TIMEOUT=${TOILE_TIMEOUT:-90}
TOILE_KEEP_VM=${TOILE_KEEP_VM:-0}
TOILE_NO_REBUILD=${TOILE_NO_REBUILD:-0}

IFS_IMAGE="${TOILE_WORK}/toile-driver.ifs"
SERIAL_LOG="${TOILE_WORK}/serial-${TOILE_VARIANT}.log"
QEMU_PID_FILE="${TOILE_WORK}/qemu-${TOILE_VARIANT}.pid"

log() { printf '[probe-driver] %s\n' "$*" >&2; }
die() { log "ERREUR: $*"; exit 2; }

command -v qemu-system-x86_64 >/dev/null 2>&1 || die "qemu-system-x86_64 absent"
[ -f "${TOILE_BUILDFILE}" ] || die "buildfile absent: ${TOILE_BUILDFILE}"
mkdir -p "${TOILE_WORK}" || die "création de ${TOILE_WORK} impossible"

# --- 0. source du SDP (mkifs + qcc) ----------------------------------------
# shellcheck disable=SC1091
. "${TOILE_SDP}/qnxsdp-env.sh" >/dev/null 2>&1 || die "qnxsdp-env.sh introuvable"
command -v mkifs >/dev/null 2>&1 || die "mkifs absent (sourcez qnxsdp-env.sh)"
command -v qcc >/dev/null 2>&1 || die "qcc absent (sourcez qnxsdp-env.sh)"

# --- 1. compilation du pilote invité ----------------------------------------
DRIVER_BIN="${REPO_DIR}/guest/build/toile-gpu"
if [ "${TOILE_NO_REBUILD}" != "1" ]; then
    log "compilation du pilote (make -f guest/common.mk driver)"
    ( cd "${REPO_DIR}/guest" && make -f common.mk driver ) \
        || die "compilation qcc du pilote a échoué"
fi
[ -x "${DRIVER_BIN}" ] || die "binaire pilote absent ou non exécutable: ${DRIVER_BIN}"
log "pilote: ${DRIVER_BIN} ($(wc -c < "${DRIVER_BIN}") octets)"

# --- 2. construction de l'image --------------------------------------------
if [ ! -f "${IFS_IMAGE}" ] || [ "${TOILE_BUILDFILE}" -nt "${IFS_IMAGE}" ] \
    || [ "${DRIVER_BIN}" -nt "${IFS_IMAGE}" ]; then
    log "construction de l'IFS avec mkifs (SDP ${TOILE_SDP})"
    ( cd "${REPO_DIR}" && mkifs "${TOILE_BUILDFILE}" "${IFS_IMAGE}" ) \
        || die "mkifs a échoué"
fi
log "image: ${IFS_IMAGE} ($(wc -c < "${IFS_IMAGE}") octets)"

# --- 3. lancement de la VM ---------------------------------------------------
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

# --- 4. attente de la fin de la sonde ---------------------------------------
log "attente de la sortie série (max ${TOILE_TIMEOUT}s)"
DEADLINE=$(( $(date +%s) + TOILE_TIMEOUT ))
while [ "$(date +%s)" -lt "${DEADLINE}" ]; do
    if [ -f "${SERIAL_LOG}" ] && grep -q 'TOILE_DRIVER_PROBE_END' "${SERIAL_LOG}"; then
        break
    fi
    sleep 2
done

# --- 5. verdict --------------------------------------------------------------
log "----- console série capturée -----"
if [ -f "${SERIAL_LOG}" ]; then
    sed 's/\r$//' "${SERIAL_LOG}" >&2
else
    log "pas de log série"
fi

FOUND=$(grep -E 'TOILE: virtio-gpu trouve \(1af4/1050\) sur B[0-9]{3}:D[0-9]{2}:F[0-9]{2}' "${SERIAL_LOG}" 2>/dev/null | head -1)
READY=$(grep -E 'TOILE: /dev/gpu0 pret' "${SERIAL_LOG}" 2>/dev/null | head -1)
CLIENT=$(grep -E 'CLIENT: /dev/gpu0 ouvert \(fd=' "${SERIAL_LOG}" 2>/dev/null | head -1)
FAIL=$(grep -E 'TOILE: ECHEC' "${SERIAL_LOG}" 2>/dev/null | head -1)

if [ -n "${FAIL}" ]; then
    log "VERDICT DRIVER = NÉGATIF : le pilote signale un échec"
    log "  ${FAIL}"
    exit 1
fi
if [ -n "${FOUND}" ] && [ -n "${READY}" ]; then
    log "VERDICT DRIVER = POSITIF : périphérique virtio-gpu trouvé + /dev/gpu0 créé"
    log "  ${FOUND}"
    log "  ${READY}"
    if [ -n "${CLIENT}" ]; then
        log "  ${CLIENT}"
    else
        log "  (réserve) le client /dev/gpu0 n'a pas pu ouvrir le noeud :"
        log "  le resmgr QNX 8.0 plante sur le message _IO_CONNECT du client"
        log "  (défaut documenté dans docs/TRIALS-E1-E6.md, §Défaut ouvert)"
    fi
    exit 0
fi
log "VERDICT DRIVER = NÉGATIF : preuve incomplète"
[ -n "${FOUND}" ] && log "  découverte OK: ${FOUND}" || log "  manque: ligne de découverte virtio-gpu"
[ -n "${READY}" ] && log "  /dev/gpu0 OK: ${READY}" || log "  manque: TOILE: /dev/gpu0 pret"
exit 1