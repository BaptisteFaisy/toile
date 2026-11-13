#!/usr/bin/env bash
# Toile — E5 : mesure du débit de peinture SUR CIBLE (invité QNX réel).
#
#   qemu/bench-e5.sh [--ifs <ifs>] [--smp 4] [--memoire 1536] [--vnc 15]
#
# Démarre l'invité QNX (image/toile-bench.build), laisse le client animé
# peindre 300 images sans cadence, en 1 puis 2 puis 4 threads de peinture,
# et affiche les lignes BENCHues sur la console série.
#
# Sans --ifs, l'image est construite (qcc + mkifs) : le SDP QNX 8.0 est
# nécessaire (poste de travail, pas la CI).
#
# Code de sortie : 0 = au moins une mesure lue, 1 = aucune mesure, 2 = erreur
# d'outillage.
set -u

TOILE_SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TOILE_REPO_DIR=$(cd -- "${TOILE_SCRIPT_DIR}/.." && pwd)

TOILE_SDP=${TOILE_SDP:-"$HOME/qnx800"}
TOILE_WORK=${TOILE_WORK:-/tmp/toile-bench}
IFS_BIN=""
SMP=4
MEM=1536
VNC_NUM=15
QEMU="${TOILE_QEMU:-$(command -v qemu-system-x86_64 || true)}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --ifs)     IFS_BIN="$2"; shift 2 ;;
        --smp)     SMP="$2"; shift 2 ;;
        --memoire) MEM="$2"; shift 2 ;;
        --vnc)     VNC_NUM="$2"; shift 2 ;;
        --aide|-h) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "bench-e5: argument inconnu : $1" >&2; exit 2 ;;
    esac
done

[ -n "$QEMU" ] || { echo "bench-e5: qemu-system-x86_64 introuvable" >&2; exit 2; }

if [ -z "$IFS_BIN" ]; then
    [ -r "$TOILE_SDP/qnxsdp-env.sh" ] || { echo "bench-e5: SDP absent ($TOILE_SDP)" >&2; exit 2; }
    mkdir -p "$TOILE_WORK"
    # shellcheck disable=SC1091
    . "$TOILE_SDP/qnxsdp-env.sh" >/dev/null 2>&1 || true
    command -v mkifs >/dev/null || { echo "bench-e5: mkifs absent" >&2; exit 2; }
    ( cd "$TOILE_REPO_DIR" && make -f guest/common.mk anim ) >/dev/null \
        || { echo "bench-e5: build invité échoué" >&2; exit 2; }
    IFS_BIN="$TOILE_WORK/bench.ifs"
    ( cd "$TOILE_REPO_DIR" && mkifs image/toile-bench.build "$IFS_BIN" ) >/dev/null \
        || { echo "bench-e5: mkifs a échoué" >&2; exit 2; }
fi
[ -f "$IFS_BIN" ] || { echo "bench-e5: IFS introuvable : $IFS_BIN" >&2; exit 2; }

mkdir -p "$TOILE_WORK"
SERIE="$TOILE_WORK/bench-serial.log"
MON="$TOILE_WORK/bench-mon.sock"
rm -f "$SERIE" "$MON"

CPU=(-cpu max)
[ -w /dev/kvm ] && CPU=(-enable-kvm -cpu host)

echo "bench-e5: image $IFS_BIN, smp=$SMP, vnc :$VNC_NUM"
setsid nohup "$QEMU" -m "$MEM" -smp "$SMP" -kernel "$IFS_BIN" -vga none \
    -device virtio-gpu-pci -vnc ":$VNC_NUM" \
    -serial "file:$SERIE" -monitor "unix:$MON,server,nowait" "${CPU[@]}" \
    >"$TOILE_WORK/bench-qemu.log" 2>&1 &
VM=$!
cleanup() { kill "$VM" 2>/dev/null; wait "$VM" 2>/dev/null; }
trap cleanup EXIT INT TERM

# L'invité met une dizaine de secondes à publier ses mesures.
for _ in $(seq 1 120); do
    grep -q "TOILE_BENCH_END" "$SERIE" 2>/dev/null && break
    sleep 1
done

grep -a "^BENCH:" "$SERIE" 2>/dev/null
if ! grep -qa "^BENCH:" "$SERIE" 2>/dev/null; then
    echo "bench-e5: aucune mesure publiée (voir $SERIE)" >&2
    exit 1
fi
echo "bench-e5: $(grep -ac '^BENCH:' "$SERIE") mesure(s), smp=$SMP"
exit 0
