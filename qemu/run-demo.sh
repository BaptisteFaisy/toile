#!/usr/bin/env bash
# Toile — DÉMO EN UNE SEULE COMMANDE : l'invité QNX 8.0 pilote le
# périphérique virtio-gpu de QEMU, la mire peinte dans l'invité apparaît dans
# la fenêtre de l'émulateur, et la capture est vérifiée numériquement.
#
# Ce que fait la commande, dans l'ordre :
#   1. compile le pilote invité et le client (qcc, via guest/common.mk) ;
#   2. construit l'image IFS de l'invité (mkifs) ;
#   3. démarre QEMU avec le périphérique virtio-gpu ;
#   4. le client ouvre /dev/gpu0, fait peindre la mire et la présente ;
#   5. capture l'affichage de QEMU (screendump HMP) et vérifie numériquement
#      que la mire y est bien ;
#   6. affiche le verdict et le chemin de la capture.
#
# Le détail de la procédure VM est factorisé dans qemu/probe-driver.sh
# (source unique) : ce script l'appelle, puis range la preuve dans
# docs/preuves/ et récapitule.
#
# Variables d'environnement :
#   TOILE_SDP        racine du SDP QNX 8.0   (défaut : ~/qnx800)
#   TOILE_WORK       répertoire de travail   (défaut : /tmp/toile-demo)
#   TOILE_VARIANT    périphérique QEMU       (défaut : virtio-gpu-pci)
#   TOILE_DISPLAY    none | vnc | egl-headless  (défaut : vnc — headless)
#   TOILE_TIMEOUT    secondes d'attente      (défaut : 120)
#   TOILE_MEM        mémoire Mo              (défaut : 1536)
#   TOILE_PROOF_DIR  dépôt des preuves       (défaut : docs/preuves)
#   TOILE_PROOF_NAME nom du PNG deposited   (défaut : toile-mire-client-1280x800.png)
#
# Code de sortie : 0 = la boucle invitée -> émulateur est prouvée de bout en
# bout (ouverture du nœud + mire visible dans la capture), 1 sinon.

set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)

TOILE_SDP=${TOILE_SDP:-"$HOME/qnx800"}
TOILE_WORK=${TOILE_WORK:-/tmp/toile-demo}
TOILE_VARIANT=${TOILE_VARIANT:-virtio-gpu-pci}
TOILE_DISPLAY=${TOILE_DISPLAY:-vnc}
TOILE_TIMEOUT=${TOILE_TIMEOUT:-120}
TOILE_MEM=${TOILE_MEM:-1536}
TOILE_PROOF_DIR=${TOILE_PROOF_DIR:-"${REPO_DIR}/docs/preuves"}
TOILE_PROOF_NAME=${TOILE_PROOF_NAME:-"toile-mire-client-1280x800.png"}
TOILE_SHOT="${TOILE_WORK}/toile-shot.png"

say() { printf '\n=== %s ===\n' "$*"; }

# --- 0. outillage -------------------------------------------------------------
command -v qemu-system-x86_64 >/dev/null 2>&1 || {
    echo "ERREUR: qemu-system-x86_64 absent" >&2; exit 2; }
[ -f "${TOILE_SDP}/qnxsdp-env.sh" ] || {
    echo "ERREUR: SDP absent: ${TOILE_SDP}" >&2; exit 2; }
command -v python3 >/dev/null 2>&1 || {
    echo "ERREUR: python3 absent (analyse de la capture)" >&2; exit 2; }
python3 -c 'import PIL' 2>/dev/null || {
    echo "ERREUR: Pillow absent (analyse de la capture)" >&2; exit 2; }
mkdir -p "${TOILE_WORK}" || {
    echo "ERREUR: ${TOILE_WORK} non créable" >&2; exit 2; }

say "Configuration"
printf '  SDP       : %s\n' "${TOILE_SDP}"
printf '  travail   : %s\n' "${TOILE_WORK}"
printf '  périphérique: %s\n' "${TOILE_VARIANT}"
printf '  affichage : %s\n' "${TOILE_DISPLAY}"
printf '  capture   : %s\n' "${TOILE_SHOT}"
printf '  preuve    : %s/%s\n' "${TOILE_PROOF_DIR}" "${TOILE_PROOF_NAME}"

# --- 1..5. compile, IFS, VM, peinture, capture, vérification ----------------
# probe-driver.sh fait les étapes 1 à 5 (source unique de la procédure VM)
# et rend son verdict sur stderr ; son code de sortie vaut 0 si la boucle
# invitée -> émulateur est complète.
say "Compilation, construction de l'image, VM QEMU, capture"
set +e
TOILE_SDP="${TOILE_SDP}" \
TOILE_WORK="${TOILE_WORK}" \
TOILE_VARIANT="${TOILE_VARIANT}" \
TOILE_DISPLAY="${TOILE_DISPLAY}" \
TOILE_TIMEOUT="${TOILE_TIMEOUT}" \
TOILE_MEM="${TOILE_MEM}" \
TOILE_SHOT="${TOILE_SHOT}" \
    "${SCRIPT_DIR}/probe-driver.sh"
RC=$?
set -e

# --- 6. dépôt de la preuve et récapitulatif ---------------------------------
if [ "${RC}" -ne 0 ]; then
    say "ÉCHEC (rc=${RC}) — voir le journal ci-dessus"
    exit "${RC}"
fi

PROOF_PATH=""
if [ -s "${TOILE_SHOT}" ]; then
    mkdir -p "${TOILE_PROOF_DIR}" 2>/dev/null || true
    if cp "${TOILE_SHOT}" "${TOILE_PROOF_DIR}/${TOILE_PROOF_NAME}" 2>/dev/null; then
        PROOF_PATH="${TOILE_PROOF_DIR}/${TOILE_PROOF_NAME}"
    fi
fi

say "Verdict"
echo "  Boucle invitée -> émulateur : PROUVÉE"
echo "  - /dev/gpu0 ouvert par un client ordinaire (plus de SIGSEGV)"
echo "  - mire peinte par l'invité et présentée au scanout virtio-gpu"
echo "  - capture de l'affichage QEMU vérifiée numériquement"
if [ -n "${PROOF_PATH}" ]; then
    echo "  - preuve déposée : ${PROOF_PATH} ($(wc -c < "${PROOF_PATH}") octets)"
else
    echo "  - capture brute (non déposée) : ${TOILE_SHOT}"
fi
exit 0