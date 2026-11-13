#!/usr/bin/env bash
# Toile — PREUVE C en une commande.
#
#   qemu/verify-hash.sh [--ifs <ifs>] [--vnc 14] [--memoire 1536]
#
# Sans --ifs, l'image de preuve est construite (qcc + mkifs, image/
# toile-verify.build) : le client animé publie l'empreinte FNV-1a de son
# framebuffer sur la série puis TIENT l'image 1,2 s.  Ce script démarre la VM,
# laisse qemu/verify-hash.py capturer l'écran pendant la tenue et comparer
# l'empreinte de l'invité avec celle recalculée sur les pixels rendus par
# QEMU.
#
# Code de sortie : 0 = empreintes identiques (transit prouvé), 1 = écart ou
# absence de preuve, 2 = erreur d'outillage.
set -u

TOILE_SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TOILE_REPO_DIR=$(cd -- "${TOILE_SCRIPT_DIR}/.." && pwd)

TOILE_SDP=${TOILE_SDP:-"$HOME/qnx800"}
TOILE_WORK=${TOILE_WORK:-/tmp/toile-verify}
IFS_BIN=""
VNC_NUM=14
MEM=1536
QEMU="${TOILE_QEMU:-$(command -v qemu-system-x86_64 || true)}"
PYTHON="${TOILE_PYTHON:-$(command -v python3 || true)}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --ifs)     IFS_BIN="$2"; shift 2 ;;
        --vnc)     VNC_NUM="$2"; shift 2 ;;
        --memoire) MEM="$2"; shift 2 ;;
        --aide|-h) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "verify-hash: argument inconnu : $1" >&2; exit 2 ;;
    esac
done

[ -n "$QEMU" ]   || { echo "verify-hash: qemu-system-x86_64 introuvable" >&2; exit 2; }
[ -n "$PYTHON" ] || { echo "verify-hash: python3 introuvable" >&2; exit 2; }
mkdir -p "$TOILE_WORK"
SERIE="$TOILE_WORK/verify-serial.log"
MON="$TOILE_WORK/verify-mon.sock"

if [ -z "$IFS_BIN" ]; then
    [ -r "$TOILE_SDP/qnxsdp-env.sh" ] || { echo "verify-hash: SDP absent ($TOILE_SDP)" >&2; exit 2; }
    # shellcheck disable=SC1091
    . "$TOILE_SDP/qnxsdp-env.sh" >/dev/null 2>&1 || true
    command -v mkifs >/dev/null 2>&1 || { echo "verify-hash: mkifs absent" >&2; exit 2; }
    ( cd "$TOILE_REPO_DIR" && make -f guest/common.mk anim driver probe ) >/dev/null \
        || { echo "verify-hash: build invité échoué" >&2; exit 2; }
    IFS_BIN="$TOILE_WORK/verify.ifs"
    ( cd "$TOILE_REPO_DIR" && mkifs image/toile-verify.build "$IFS_BIN" ) >/dev/null \
        || { echo "verify-hash: mkifs a échoué" >&2; exit 2; }
fi
[ -f "$IFS_BIN" ] || { echo "verify-hash: IFS introuvable : $IFS_BIN" >&2; exit 2; }

rm -f "$SERIE" "$MON"
CPU=(-cpu max); [ -w /dev/kvm ] && CPU=(-enable-kvm -cpu host)
echo "verify-hash: image $IFS_BIN, vnc :$VNC_NUM"
setsid nohup "$QEMU" -m "$MEM" -kernel "$IFS_BIN" -vga none \
    -device virtio-gpu-pci -vnc ":$VNC_NUM" \
    -serial "file:$SERIE" -monitor "unix:$MON,server,nowait" "${CPU[@]}" \
    >"$TOILE_WORK/verify-qemu.log" 2>&1 &
VM=$!
cleanup() { kill "$VM" 2>/dev/null; wait "$VM" 2>/dev/null; }
trap cleanup EXIT INT TERM

# L'invité met ~8 s à peindre sa première image tenue.
for _ in $(seq 1 60); do grep -q "HASH:" "$SERIE" 2>/dev/null && break; sleep 1; done
grep -q "HASH:" "$SERIE" 2>/dev/null || { echo "verify-hash: aucune empreinte publiée (voir $SERIE)" >&2; exit 1; }

"$PYTHON" "$TOILE_SCRIPT_DIR/verify-hash.py" --serial "$SERIE" --monitor "$MON"
rc=$?
echo "verify-hash: verdict = $([ $rc -eq 0 ] && echo 'PREUVE C ACQUISE' || echo 'PREUVE C NON ACQUISE')"
exit $rc