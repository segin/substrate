#!/bin/bash
#
# build-image.sh - build a /perso/venix disk image from the floppies of a
# Venix/86 2.1 system.
#
# REQUIRES EXTERNAL MEDIA.  Nothing this script installs is in the
# repository, and it cannot run without files you supply: floppy images
# of VenturCom Venix/86.  It builds no part of substrate and no other
# script or test depends on it.
#
#   What to provide    the directory of floppy images from the archive
#                      WinWorld keeps as "Venix-86 2.1 (1985)
#                      (5.25-1.2mb)": BACKUP1.IMG ... BACKUP9.IMG.  (The
#                      XFER.IMG beside them is the boot floppy and is not
#                      read.)
#   How                unpack the archive and pass that directory with -m:
#
#                          7z x 'Venix-86 2.1 (1985) (5.25-1.2mb).7z'
#                          tools/venix/build-image.sh \
#                              -m 'Venix-86 2.1 (1985) (5.25-1.2mb)' \
#                              -o venix86.img
#
#   What comes out     OUTPUT, an ext2 image.  It holds VenturCom's
#                      software and is not to be committed either (*.img
#                      is ignored).
#
# The archive is not installation media but a tar backup of a system that
# was in use, nine floppies of it, so the image is that system: Venix/86
# with its C compiler, and its owner's files beside it.  venixtar.py reads
# the floppies and says what the format is.  Owners, modes and hard links
# are the backup's; /dev is a directory of ordinary files as tar left it,
# and substrate gives a Venix program its own devices.
#
# The image is ext2 rather than a V7 filesystem because substrate reads it
# through its own VFS and runs the programs under the Venix personality.
# Mount it at /perso/venix.
#
# Usage: build-image.sh -m MEDIA_DIR -o OUTPUT [-s SIZE_MB] [-l LABEL]

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
MEDIA=
OUT=
SIZE_MB=32
LABEL=venix

usage() {
    sed -n '2,/^# Usage/p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case $1 in
        -m) MEDIA=$2; shift 2 ;;
        -o) OUT=$2; shift 2 ;;
        -s) SIZE_MB=$2; shift 2 ;;
        -l) LABEL=$2; shift 2 ;;
        -h|--help) usage 0 ;;
        *) echo "build-image.sh: unknown argument: $1" >&2; usage 2 ;;
    esac
done

if [ -z "$MEDIA" ] || [ -z "$OUT" ]; then
    echo "build-image.sh: -m MEDIA_DIR and -o OUTPUT are required." >&2
    echo "This script needs floppy images that are not in the" >&2
    echo "repository; run it with --help for what to obtain." >&2
    exit 2
fi
[ -d "$MEDIA" ] || { echo "build-image.sh: no such directory: $MEDIA" >&2; exit 1; }
for tool in mke2fs fakeroot e2fsck python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-image.sh: $tool is required" >&2; exit 1; }
done

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/veniximg.XXXXXX")
trap 'chmod -R u+rwx "$STAGE" 2>/dev/null; rm -rf "$STAGE"' EXIT
ROOT=$STAGE/root
mkdir -p "$ROOT"

vols=()
while IFS= read -r v; do vols+=("$v"); done \
    < <(find "$MEDIA" -maxdepth 1 -iname 'BACKUP[0-9]*.IMG' | sort -V)
if [ ${#vols[@]} -eq 0 ]; then
    echo "build-image.sh: no BACKUPn.IMG in $MEDIA" >&2
    exit 1
fi
echo "==> $(basename "$MEDIA")"
printf '    %d floppies  ' ${#vols[@]}
python3 "$HERE/venixtar.py" extract "$ROOT" "${vols[@]}"

# A program needs somewhere to write that the backup did not bring.
mkdir -p "$ROOT/tmp"

mv "$ROOT/.manifest" "$STAGE/manifest"
rm -f "$OUT"
fakeroot -- bash -c '
    set -eu
    root=$1 manifest=$2 out=$3 size=$4 label=$5
    while read -r type mode uid gid major minor mtime path target; do
        if [ "$type" = l ]; then
            rm -f "$root$path"; ln "$root$target" "$root$path"; continue
        fi
        chown "$uid:$gid" "$root$path"
        chmod "$mode" "$root$path"
        touch -c -d "@$mtime" "$root$path"
    done < <(grep -v "^l " "$manifest"; grep "^l " "$manifest" || true)
    chmod 1777 "$root/tmp"
    mke2fs -q -t ext2 -L "$label" -d "$root" "$out" "${size}M"
' fakeroot-sh "$ROOT" "$STAGE/manifest" "$OUT" "$SIZE_MB" "$LABEL"

e2fsck -fn "$OUT" >/dev/null
echo "==> $OUT: $(grep -c . "$STAGE/manifest") entries, label $LABEL"
