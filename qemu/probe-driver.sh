#!/usr/bin/env bash
# Toile — sonde du pilote invité + preuve visuelle (essai E6, fermeture de
# la boucle invité -> émulateur).
#
# Procédure :
#   1. compilation du pilote invité (guest/common.mk driver) et du client ;
#   2. construction de l'image IFS (image/toile-driver.build) avec mkifs ;
#   3. démarrage d'une VM QEMU avec le périphérique virtio-gpu ;
#   4. capture de la console série jusqu'au marqueur
#      "=== TOILE_DRIVER_PROBE_END ===" ;
#   5. si TOILE_DISPLAY != none : capture de l'affichage QEMU (screendump HMP)
#      et vérification numérique du contenu de l'image ;
#   6. verdict.
#
# Variables d'environnement :
#   TOILE_SDP        racine du SDP          (défaut : ~/qnx800)
#   TOILE_BUILDFILE  buildfile de l'image   (défaut : image/toile-driver.build)
#   TOILE_WORK       répertoire de travail  (défaut : /tmp/toile-driver)
#   TOILE_VARIANT    périphérique QEMU      (défaut : virtio-gpu-pci)
#   TOILE_MEM        mémoire Mo             (défaut : 1536)
#   TOILE_TIMEOUT    secondes d'attente     (défaut : 90)
#   TOILE_DISPLAY    none | vnc | egl-headless   (défaut : none)
#   TOILE_VNC_NUM    numéro d'écran VNC     (défaut : 7)
#   TOILE_SHOT       chemin de la capture   (défaut : $TOILE_WORK/toile-shot.png)
#   TOILE_KEEP_VM    1 = ne pas arrêter la VM à la fin
#   TOILE_NO_REBUILD 1 = ne pas recompiler le pilote (il doit exister)
#
# Code de sortie : 0 si le pilote trouve le périphérique, crée /dev/gpu0, que
# le client l'ouvre et peint la mire (et, en mode capture, que l'image
# contient la mire) ; 1 sinon ; 2 en cas d'erreur d'outillage.

set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)

TOILE_SDP=${TOILE_SDP:-"$HOME/qnx800"}
TOILE_BUILDFILE=${TOILE_BUILDFILE:-"${REPO_DIR}/image/toile-driver.build"}
TOILE_WORK=${TOILE_WORK:-/tmp/toile-driver}
TOILE_VARIANT=${TOILE_VARIANT:-virtio-gpu-pci}
TOILE_MEM=${TOILE_MEM:-1536}
TOILE_TIMEOUT=${TOILE_TIMEOUT:-90}
TOILE_DISPLAY=${TOILE_DISPLAY:-none}
TOILE_VNC_NUM=${TOILE_VNC_NUM:-7}
TOILE_SHOT=${TOILE_SHOT:-"${TOILE_WORK}/toile-shot.png"}
TOILE_KEEP_VM=${TOILE_KEEP_VM:-0}
TOILE_NO_REBUILD=${TOILE_NO_REBUILD:-0}

IFS_IMAGE="${TOILE_WORK}/toile-driver.ifs"
SERIAL_LOG="${TOILE_WORK}/serial-${TOILE_VARIANT}.log"
QEMU_PID_FILE="${TOILE_WORK}/qemu-${TOILE_VARIANT}.pid"
MON_SOCK="${TOILE_WORK}/monitor-${TOILE_VARIANT}.sock"
PPM_SHOT="${TOILE_WORK}/toile-shot.ppm"

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

# --- 1. compilation du pilote invité et du client ---------------------------
DRIVER_BIN="${REPO_DIR}/guest/build/toile-gpu"
CLIENT_BIN="${REPO_DIR}/guest/build/toile-gpu-probe"
if [ "${TOILE_NO_REBUILD}" != "1" ]; then
    log "compilation du pilote et du client (make -f guest/common.mk driver probe)"
    ( cd "${REPO_DIR}/guest" && make -f common.mk driver probe ) \
        || die "compilation qcc a échoué"
fi
[ -x "${DRIVER_BIN}" ] || die "binaire pilote absent: ${DRIVER_BIN}"
[ -x "${CLIENT_BIN}" ] || die "binaire client absent: ${CLIENT_BIN}"
log "pilote: ${DRIVER_BIN} ($(wc -c < "${DRIVER_BIN}") octets)"

# --- 2. construction de l'image --------------------------------------------
if [ ! -f "${IFS_IMAGE}" ] || [ "${TOILE_BUILDFILE}" -nt "${IFS_IMAGE}" ] \
    || [ "${DRIVER_BIN}" -nt "${IFS_IMAGE}" ] \
    || [ "${CLIENT_BIN}" -nt "${IFS_IMAGE}" ]; then
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
    -serial "file:${SERIAL_LOG}"
    -pidfile "${QEMU_PID_FILE}"
)
rm -f "${MON_SOCK}"
case "${TOILE_DISPLAY}" in
    vnc)
        QEMU_ARGS+=(-vnc ":${TOILE_VNC_NUM}")
        QEMU_ARGS+=(-monitor "unix:${MON_SOCK},server,nowait")
        log "affichage VNC actif sur :${TOILE_VNC_NUM} (capture possible)"
        ;;
    egl-headless)
        QEMU_ARGS+=(-display egl-headless)
        QEMU_ARGS+=(-monitor "unix:${MON_SOCK},server,nowait")
        log "affichage egl-headless (capture possible)"
        ;;
    none|"")
        QEMU_ARGS+=(-display none -monitor none)
        log "affichage désactivé (aucune capture possible)"
        ;;
    *)
        die "TOILE_DISPLAY inconnu: ${TOILE_DISPLAY} (none|vnc|egl-headless)"
        ;;
esac
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
        wait "${QEMU_PID}" 2>/dev/null
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
OPENED=$(grep -E 'CLIENT: /dev/gpu0 ouvert \(fd=' "${SERIAL_LOG}" 2>/dev/null | head -1)
MIRE=$(grep -E 'CLIENT: mire peinte et presentee' "${SERIAL_LOG}" 2>/dev/null | head -1)
CLIENT_OK=$(grep -E '^CLIENT: OK' "${SERIAL_LOG}" 2>/dev/null | head -1)
FAIL=$(grep -E 'TOILE: ECHEC|CLIENT: ECHEC' "${SERIAL_LOG}" 2>/dev/null | head -1)

if [ -n "${FAIL}" ]; then
    log "VERDICT DRIVER = NÉGATIF : échec signalé"
    log "  ${FAIL}"
    exit 1
fi

RC=1
if [ -n "${FOUND}" ] && [ -n "${READY}" ] && [ -n "${OPENED}" ] \
    && [ -n "${MIRE}" ] && [ -n "${CLIENT_OK}" ]; then
    log "VERDICT DRIVER = POSITIF"
    log "  ${FOUND}"
    log "  ${READY}"
    log "  ${OPENED}"
    log "  ${MIRE}"
    RC=0
else
    log "VERDICT DRIVER = NÉGATIF : preuve incomplète"
    [ -n "${FOUND}" ] || log "  manque: ligne de découverte virtio-gpu"
    [ -n "${READY}" ] || log "  manque: TOILE: /dev/gpu0 pret"
    [ -n "${OPENED}" ] || log "  manque: CLIENT: /dev/gpu0 ouvert"
    [ -n "${MIRE}" ] || log "  manque: CLIENT: mire peinte et presentee"
    [ -n "${CLIENT_OK}" ] || log "  manque: CLIENT: OK"
    exit 1
fi

# --- 6. capture de l'affichage (preuve visuelle B) ---------------------------
if [ "${TOILE_DISPLAY}" = "none" ] || [ "${TOILE_DISPLAY}" = "" ]; then
    log "capture: non demandée (TOILE_DISPLAY=none)"
    exit "${RC}"
fi

log "capture de l'affichage QEMU (screendump HMP) vers ${PPM_SHOT}"
rm -f "${PPM_SHOT}"
MON_SOCK="${MON_SOCK}" PPM_SHOT="${PPM_SHOT}" python3 - <<'PY' \
    || { log "VERDICT CAPTURE = ÉCHEC : screendump impossible"; exit 1; }
import os, socket, sys, time
sock = os.environ["MON_SOCK"]
out = os.environ["PPM_SHOT"]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
try:
    s.connect(sock)
except OSError as e:
    sys.stderr.write("capture: connexion monitor échouée: %s\n" % e)
    sys.exit(1)
time.sleep(0.5)
try:
    s.recv(65536)
except OSError:
    pass
s.sendall(b"screendump " + out.encode() + b"\n")
deadline = time.time() + 10.0
while time.time() < deadline:
    if os.path.exists(out) and os.path.getsize(out) > 0:
        sys.exit(0)
    time.sleep(0.3)
sys.stderr.write("capture: screendump n'a pas produit de fichier\n")
sys.exit(1)
PY
[ -s "${PPM_SHOT}" ] || { log "VERDICT CAPTURE = ÉCHEC : PPM vide"; exit 1; }

log "analyse numérique de la capture"
if PPM_SHOT="${PPM_SHOT}" TOILE_SHOT="${TOILE_SHOT}" python3 - <<'PY'
import os, sys
from PIL import Image

ppm = os.environ["PPM_SHOT"]
out = os.environ["TOILE_SHOT"]
im = Image.open(ppm).convert("RGB")
w, h = im.size
px = im.load()

# La mire (guest/common/toile_pattern.c) : 8 barres pleines sur les 3/4
# supérieurs (blanc, jaune, cyan, vert, magenta, rouge, bleu, noir), puis
# dégradé de gris du 3/4 au 7/8, puis damier 16 px sur le dernier 1/8.
# On rejoue ces règles sur l'image capturée : c'est la preuve numérique que
# les pixels de l'invité ont bien traversé QEMU.
bars = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
errors = []
y_bar = h // 8                       # milieu de la zone des barres
for i, c in enumerate(bars):
    x = (i * w) // 8 + (w // 16)     # centre de chaque barre
    got = px[x, y_bar]
    if got != c:
        errors.append("barre %d : attendu %s, lu %s" % (i, c, got))

# Dégradé de gris : deux points de part et d'autre, valeurs croissantes.
y_grad = (h * 3 // 4 + h * 7 // 8) // 2
g_lo = px[w // 8, y_grad]
g_hi = px[(w * 7) // 8, y_grad]
if not (g_lo[0] == g_lo[1] == g_lo[2] and g_hi[0] == g_hi[1] == g_hi[2]):
    errors.append("dégradé : non gris (%s -> %s)" % (g_lo, g_hi))
if not g_lo[0] < g_hi[0]:
    errors.append("dégradé : non croissant (%s -> %s)" % (g_lo, g_hi))

# Damier : deux cases contiguës de couleurs différentes.
y_chk = (h * 7 // 8) + 32
c1 = px[8, y_chk]
c2 = px[24, y_chk]
if c1 == c2:
    errors.append("damier : deux cases identiques (%s)" % (c1,))

# Statistiques globales : l'image ne doit pas être noire.
counts = {}
for y in range(h):
    for x in range(w):
        c = px[x, y]
        counts[c] = counts.get(c, 0) + 1
total = w * h
non_black = sum(n for c, n in counts.items() if c != (0, 0, 0))

im.save(out, optimize=True)
print("capture: %dx%d, %d couleurs distinctes, non-noirs=%d (%.1f%%)"
      % (w, h, len(counts), non_black, 100.0 * non_black / total))
print("  barres     : %s" % ("OK" if not errors else "KO"))
print("  dégradé    : %s -> %s" % (g_lo, g_hi))
print("  damier     : %s / %s" % (c1, c2))
for e in errors:
    sys.stderr.write("  ERREUR: %s\n" % e)
if errors or non_black < total // 2:
    sys.exit(1)
sys.exit(0)
PY
then
    log "VERDICT CAPTURE = POSITIF : la mire est visible dans QEMU"
    log "  capture: ${TOILE_SHOT}"
    exit "${RC}"
fi
log "VERDICT CAPTURE = NÉGATIF : l'image ne contient pas la mire attendue"
log "  capture conservée pour analyse: ${TOILE_SHOT}"
exit 1