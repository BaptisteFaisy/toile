#!/usr/bin/env bash
# Toile — publier l'image IFS invitee comme asset de release GitHub.
#
#   qemu/publish-ifs.sh --tag <tag> [--ifs-build <fichier.build>] [--dry-run]
#
# A EXECUTER SUR LE POSTE DE TRAVAIL (la machine ou vit le SDP QNX 8.0),
# JAMAIS dans la CI : la CI ne compile pas l'invite, elle telecharge l'asset
# publie ici.
#
# Etapes :
#   1. compile les binaires invites (make -f guest/common.mk ...) ;
#   2. construit l'image IFS (mkifs image/toile-vitrine.build) ;
#   3. publie l'image comme asset `toile-guest.ifs` de la release <tag>.
#
# Garde-fous :
#   --dry-run       construit tout, ne publie rien (mode de test) ;
#   --allow-new-tag autorise la creation du tag (il doit deja exister sinon) ;
#   --allow-branch  autorise la publication hors de la branche main.
#
# Code de sortie : 0 = publie (ou construit en dry-run), 1 = refus, 2 = erreur.
set -euo pipefail

TOILE_SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TOILE_REPO_DIR=$(cd -- "${TOILE_SCRIPT_DIR}/.." && pwd)

TOILE_SDP=${TOILE_SDP:-"$HOME/qnx800"}
IFS_BUILD="image/toile-vitrine.build"
TAG=""
DRY_RUN=0
ALLOW_NEW_TAG=0
ALLOW_BRANCH=0
ASSET_NAME="toile-guest.ifs"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --tag)          TAG="$2"; shift 2 ;;
        --ifs-build)    IFS_BUILD="$2"; shift 2 ;;
        --dry-run)      DRY_RUN=1; shift ;;
        --allow-new-tag) ALLOW_NEW_TAG=1; shift ;;
        --allow-branch) ALLOW_BRANCH=1; shift ;;
        --aide|-h)      sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "publish-ifs: argument inconnu : $1" >&2; exit 2 ;;
    esac
done

[ -n "$TAG" ] || { echo "publish-ifs: --tag obligatoire" >&2; exit 1; }

# --- garde-fous ------------------------------------------------------------
BRANCH=$(git -C "$TOILE_REPO_DIR" rev-parse --abbrev-ref HEAD)
if [ "$BRANCH" != "main" ] && [ "$ALLOW_BRANCH" -ne 1 ]; then
    echo "publish-ifs: branche $BRANCH (main attendue, ou --allow-branch)" >&2
    exit 1
fi
if [ "$DRY_RUN" -eq 0 ] && ! git -C "$TOILE_REPO_DIR" rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    if [ "$ALLOW_NEW_TAG" -ne 1 ]; then
        echo "publish-ifs: tag $TAG absent (ou --allow-new-tag pour le creer)" >&2
        exit 1
    fi
fi

# --- toolchain -------------------------------------------------------------
[ -r "$TOILE_SDP/qnxsdp-env.sh" ] || {
    echo "publish-ifs: SDP QNX absent ($TOILE_SDP) : executer sur le poste de travail" >&2
    exit 2
}
# shellcheck disable=SC1091
. "$TOILE_SDP/qnxsdp-env.sh" >/dev/null 2>&1 || true
command -v mkifs >/dev/null || { echo "publish-ifs: mkifs absent" >&2; exit 2; }

cd "$TOILE_REPO_DIR"
echo "publish-ifs: construction des binaires invites"
make -f guest/common.mk anim driver probe input >/dev/null

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
IFS_BIN="$WORK/$ASSET_NAME"
echo "publish-ifs: mkifs $IFS_BUILD"
mkifs "$IFS_BUILD" "$IFS_BIN" >/dev/null
SIZE=$(stat -c %s "$IFS_BIN")
echo "publish-ifs: image $IFS_BIN ($SIZE octets)"

if [ "$DRY_RUN" -eq 1 ]; then
    echo "publish-ifs: dry-run, rien n'est publie"
    exit 0
fi

command -v gh >/dev/null || { echo "publish-ifs: gh absent (poster la release depuis une machine qui l'a)" >&2; exit 2; }

# --- publication -----------------------------------------------------------
if [ "$ALLOW_NEW_TAG" -eq 1 ]; then
    git tag -a "$TAG" -m "Image invitee Toile $TAG" && git push origin "refs/tags/$TAG"
fi

if gh release view "$TAG" >/dev/null 2>&1; then
    echo "publish-ifs: mise a jour de la release $TAG"
    gh release upload "$TAG" "$IFS_BIN" --clobber
else
    echo "publish-ifs: creation de la release $TAG"
    gh release create "$TAG" "$IFS_BIN" \
        --title "Toile guest image $TAG" \
        --notes "Image IFS de l'invite QNX 8.0 (pilote virtio-gpu + client anime + clavier virtio-input). Construite sur le poste de travail avec le SDP QNX 8.0. La CI de bout en bout telecharge cet asset et rejoue la preuve C."
fi

URL=$(gh release view "$TAG" --json assets --jq '.assets[] | select(.name=="'"$ASSET_NAME"'") | .url')
echo "publish-ifs: publie -> $URL"
