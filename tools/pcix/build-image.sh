#!/bin/bash
#
# build-image.sh - build a /perso/pcix disk image from the distribution
# floppies of IBM PC/IX 1.0.
#
# REQUIRES EXTERNAL MEDIA.  Nothing this script installs is in the
# repository, and it cannot run without files you supply: the nineteen
# 360K floppy images of PC/IX.  It builds no part of substrate and no other
# script or test depends on it.
#
#   What to provide    the directory of raw floppy images from the archive
#                      WinWorld keeps as "PC-IX 1.0 (5.25-360k)": its IMA/
#                      directory, holding 01MAINT.IMA, 02CORE1.IMA ...
#                      19ACCNT.IMA.  (The IMD/ directory beside it is the
#                      same floppies in ImageDisk form and is not read.)
#   How                unpack the archive and pass that directory with -m:
#
#                          7z x 'PC-IX 1.0 (5.25-360k).7z'
#                          tools/pcix/build-image.sh \
#                              -m 'PC-IX 1.0 (5.25-360k)/IMA' -o pcix.img
#
#   What comes out     OUTPUT, an ext2 image.  It holds IBM's and
#                      INTERACTIVE's software and is not to be committed
#                      either (*.img is ignored).
#
# The image is ext2 rather than a System III filesystem because substrate
# reads it through its own VFS and runs the programs under the PC/IX
# personality: the container is substrate's business, only the file
# contents are PC/IX's.  Mount it at /perso/pcix.
#
# WHAT IS INSTALLED
#   The Core system (02CORE1..09CORE8, one backup archive by inode) and
#   then each optional subset over it (Programming, Communications, SCCS,
#   Text Processing, Special Purpose, Games, Accounting; archives by name).
#   pcixbackup.py reads both and says what the formats are.  The
#   Maintenance floppy holds only the standalone installation tools and is
#   left out; so are the subsets' installation scripts, which are not run.
#
#   Owners, modes, devices and hard links are the media's.  /dev is kept
#   as a record of what PC/IX had; substrate gives a PC/IX program its own
#   devices.
#
# Usage: build-image.sh -m MEDIA_DIR -o OUTPUT [-s SIZE_MB] [-l LABEL]

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
MEDIA=
OUT=
SIZE_MB=32
LABEL=pcix

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
    echo "This script needs distribution media that is not in the" >&2
    echo "repository; run it with --help for what to obtain." >&2
    exit 2
fi
[ -d "$MEDIA" ] || { echo "build-image.sh: no such directory: $MEDIA" >&2; exit 1; }
for tool in mke2fs fakeroot e2fsck python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-image.sh: $tool is required" >&2; exit 1; }
done

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/pciximg.XXXXXX")
trap 'chmod -R u+rwx "$STAGE" 2>/dev/null; rm -rf "$STAGE"' EXIT
ROOT=$STAGE/root
mkdir -p "$ROOT"

# The volumes of one archive, by the names the floppies were given.
volumes() {
    find "$MEDIA" -maxdepth 1 -iname "$1" | sort
}

unpack() {
    local what=$1 pattern=$2
    local vols=()
    while IFS= read -r v; do vols+=("$v"); done < <(volumes "$pattern")
    if [ ${#vols[@]} -eq 0 ]; then
        echo "    $what: no floppy matching $pattern in $MEDIA" >&2
        return 1
    fi
    printf '    %-20s %2d floppies  ' "$what" ${#vols[@]}
    python3 "$HERE/pcixbackup.py" extract "$ROOT" "${vols[@]}"
}

echo "==> $(basename "$MEDIA")"
unpack "Core system" '0[2-9]CORE?.IMA'
unpack "Programming"        '1[0-3]PROG?.IMA' || true
unpack "Communications"     '14COMMS.IMA'     || true
unpack "SCCS"               '15SCCS.IMA'      || true
unpack "Text Processing"    '16TEXT.IMA'      || true
unpack "Special Purpose"    '17SPECIA.IMA'    || true
unpack "Games"              '18GAMES.IMA'     || true
unpack "Accounting"         '19ACCNT.IMA'     || true

# Apply the manifest -- owners, modes, devices, links -- and make the
# filesystem, all as the root fakeroot pretends to be.
MANIFEST=$ROOT/.manifest
mv "$MANIFEST" "$STAGE/manifest"
rm -f "$OUT"
fakeroot -- bash -c '
    set -eu
    root=$1 manifest=$2 out=$3 size=$4 label=$5
    while read -r type mode uid gid major minor mtime path target; do
        case $type in
            l)  rm -f "$root$path"; ln "$root$target" "$root$path"; continue ;;
            c|b) rm -f "$root$path"; mknod "$root$path" "$type" "$major" "$minor" ;;
            p)  rm -f "$root$path"; mknod "$root$path" p ;;
        esac
        chown "$uid:$gid" "$root$path"
        chmod "$mode" "$root$path"
        touch -c -d "@$mtime" "$root$path"
    done < <(grep -v "^l " "$manifest"; grep "^l " "$manifest" || true)
    mke2fs -q -t ext2 -L "$label" -d "$root" "$out" "${size}M"
' fakeroot-sh "$ROOT" "$STAGE/manifest" "$OUT" "$SIZE_MB" "$LABEL"

e2fsck -fn "$OUT" >/dev/null
echo "==> $OUT: $(grep -c . "$STAGE/manifest") entries, label $LABEL"
