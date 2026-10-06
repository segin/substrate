#!/bin/bash
#
# build-image.sh - build a /perso/sunos disk image from the distribution
# floppies of SunOS 4.0 for the Sun386i.
#
# REQUIRES EXTERNAL MEDIA.  Nothing this script installs is in the
# repository, and it cannot run without files you supply: the floppy images
# of a Sun386i SunOS release.  It builds no part of substrate and no other
# script or test depends on it.
#
#   What to provide    a directory holding the release's 3.5" floppies as
#                      raw *.img files, one per floppy -- e.g. the contents
#                      of "SunOS 4.01 Sun386i.rar" (SunOS 4.0.1, 35 floppies)
#                      from the Tenox archive,
#                      tenox.pdp-11.net/os/sunos/sun386i/.
#   How                unpack the archive and pass the directory that then
#                      contains the .img files:
#
#                          mkdir sunos401
#                          bsdtar xf "SunOS 4.01 Sun386i.rar" -C sunos401
#                          tools/sunos/build-image.sh -m sunos401 -o sun386i.img
#
#   What comes out     OUTPUT, an ext2 image.  It holds the vendor's
#                      software and is not to be committed either (*.img is
#                      ignored).
#
# The image is ext2 rather than a 4.2BSD filesystem because substrate reads
# it through its own VFS; only the file contents are Sun's.  The programs
# are i386 COFF, most of them linked against the shared /usr/lib/libc.so,
# and are for the SunOS personality: mount the image at /perso/sunos.
#
# WHAT THE MEDIA LOOKS LIKE
#   Every floppy but the two that boot the installation and the diagnostics
#   one is a volume of a bar(1) archive (read with bar.py, which describes
#   the format).  A volume carries its archive's title and its own number,
#   so the floppies are sorted by what they say and not by their file
#   names.  The archives, and where each goes:
#
#       root file system          /
#       /usr file system          /usr
#       /files file system        /files
#       Applications Supplement   /files/cluster/RELEASE/appl
#       Developer's Toolkit       /files/cluster/RELEASE/devel
#
#   RELEASE is the name after the colon in the titles, sun386.sunos4.0.1.
#   The last two are the optional "clusters", one directory each; /usr is
#   full of symbolic links into them through /usr/cluster, which a Sun386i
#   has as a mount of the cluster directory and the image has as a link to
#   it.
#
#   Symbolic links to absolute paths are made relative, so that they stay
#   inside /perso/sunos.  Device nodes are not made.  Modes and owners are
#   the archives'.
#
#   The SunOS 4.0.2 Upgrade Kit floppies in the same set are not applied:
#   they are installed by a script that picks files by what the machine
#   already has, not laid down whole.
#
# Usage: build-image.sh -m MEDIA_DIR -o OUTPUT [-s SIZE_MB] [-l LABEL]

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
BAR=$HERE/bar.py
MEDIA=
OUT=
SIZE_MB=192
LABEL=sunos

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
if [ ! -d "$MEDIA" ] || ! ls "$MEDIA"/*.img >/dev/null 2>&1; then
    echo "build-image.sh: no *.img floppy images in '$MEDIA'." >&2
    echo "Run with --help for what to provide." >&2
    exit 1
fi
for tool in mke2fs fakeroot debugfs e2fsck python3 gzip realpath; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-image.sh: $tool is required" >&2; exit 1; }
done

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/sunosimg.XXXXXX")
trap 'chmod -R u+rwx "$STAGE" 2>/dev/null; rm -rf "$STAGE"' EXIT
ROOT=$STAGE/root
MODES=$STAGE/modes
INDEX=$STAGE/index
mkdir -p "$ROOT"
: > "$MODES"
: > "$INDEX"

# "NUMBER<tab>TITLE<tab>FILE" for every bar volume.
for f in "$MEDIA"/*.img; do
    if t=$(python3 "$BAR" title "$f" 2>/dev/null); then
        printf '%s\t%s\n' "$t" "$f" >> "$INDEX"
    else
        echo "    (skipping $(basename "$f"): not a bar volume)"
    fi
done

RELEASE=$(awk -F'\t' '$2 ~ /^root file system:/ { sub(/^[^:]*:/, "", $2); print $2; exit }' "$INDEX")
if [ -z "$RELEASE" ]; then
    echo "build-image.sh: no \"root file system\" floppy in '$MEDIA'." >&2
    exit 1
fi
CLUSTER=files/cluster/$RELEASE

# lay TITLE-PREFIX DIRECTORY: extract that archive's volumes, in order.
lay() {
    local title=$1 where=$2 vols=() n
    while IFS=$'\t' read -r _ _ f; do
        vols+=("$f")
    done < <(awk -F'\t' -v t="$title" 'index($2, t) == 1' "$INDEX" | sort -n)
    if [ ${#vols[@]} -eq 0 ]; then
        printf '    %-26s (not in the media)\n' "$title"
        return 0
    fi
    mkdir -p "$ROOT/$where"
    n=$(python3 "$BAR" extract "$ROOT/$where" "${vols[@]}")
    # Modes as recorded, at the paths the files have in the image.
    sed "s# \\([^ ]*\\)\$# ${where:+$where/}\\1#" "$ROOT/$where/.modes" >> "$MODES"
    rm -f "$ROOT/$where/.modes"
    printf '    %-26s %2d floppies, %5d file(s) -> /%s\n' "$title" "${#vols[@]}" "$n" "$where"
}

echo "==> $RELEASE"
lay "root file system:" ""
lay "/usr file system:" usr
lay "/files file system:" files
lay "Applications Supplement:" "$CLUSTER/appl"
lay "Developer's Toolkit:" "$CLUSTER/devel"

# /usr/cluster is where a Sun386i mounts the cluster directory.
if [ -d "$ROOT/usr/cluster" ] && [ ! -L "$ROOT/usr/cluster" ]; then
    rmdir "$ROOT/usr/cluster" 2>/dev/null || true
fi
[ -e "$ROOT/usr/cluster" ] || ln -s "../$CLUSTER" "$ROOT/usr/cluster"

# Absolute symbolic links would name substrate's own files.
while IFS= read -r -d '' l; do
    t=$(readlink "$l")
    case $t in
        /*) ln -sfn "$(realpath -m --relative-to="$(dirname "$l")" "$ROOT$t")" "$l" ;;
    esac
done < <(find "$ROOT" -type l -print0)
dangling=$(find "$ROOT" -xtype l | wc -l)

chmod -R u+rwX "$ROOT"
files=$(find "$ROOT" -type f | wc -l)
inodes=$(( $(find "$ROOT" | wc -l) + 2048 ))
echo "==> building $OUT (${SIZE_MB} MB, $files files, $dangling dangling link(s))"
rm -f "$OUT"
fakeroot -- sh -c "
    chown -R 0:0 '$ROOT'
    mke2fs -q -F -b 1024 -t ext2 -L '$LABEL' -N $inodes \
        -d '$ROOT' '$OUT' ${SIZE_MB}M
"

# Modes and owners, in the image: many files are unreadable to their owner
# and could not have been staged that way.
CMDS=$STAGE/debugfs.cmd
: > "$CMDS"
tac "$MODES" | awk '!seen[$4]++' | while read -r mode uid gid path; do
    [ -e "$ROOT/$path" ] || continue
    [ -L "$ROOT/$path" ] && continue
    if [ -d "$ROOT/$path" ]; then t=040000; else t=0100000; fi
    printf 'sif "/%s" mode 0%o\n' "$path" $(( t | 0$mode )) >> "$CMDS"
    [ "$uid" = 0 ] || printf 'sif "/%s" uid %d\n' "$path" "$uid" >> "$CMDS"
    [ "$gid" = 0 ] || printf 'sif "/%s" gid %d\n' "$path" "$gid" >> "$CMDS"
done
debugfs -w -f "$CMDS" "$OUT" >/dev/null 2>&1

e2fsck -fn "$OUT" 2>&1 | tail -1 | sed 's/^/    /'
echo "    shell: $(debugfs -R 'stat /sbin/sh' "$OUT" 2>/dev/null | grep -o 'Size: [0-9]*' | head -1)"
echo "    libc:  $(debugfs -R 'stat /usr/lib/libc.so.2.0' "$OUT" 2>/dev/null | grep -o 'Size: [0-9]*' | head -1)"
echo "Mount at /perso/sunos:  mount <device> /perso/sunos ext2"
