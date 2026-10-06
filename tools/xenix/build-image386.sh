#!/bin/sh
#
# build-image386.sh - build a /perso/xenix disk image from a set of SCO
# Xenix/386 distribution floppies.
#
# REQUIRES EXTERNAL MEDIA.  Nothing this script installs is in the
# repository, and it cannot run without files you supply: the floppy images
# of a SCO Xenix/386 release.  It builds no part of substrate and no other
# script or test depends on it.
#
#   What to provide    a directory holding the release's floppy images as
#                      *.img files, one per floppy, as archived by
#                      WinWorld -- e.g. "SCO Xenix 386 2.2.3c (3.5-720k).7z"
#                      or "SCO Xenix 386 2.3.4q (3.5).7z".
#   How                unpack the archive (7z x ARCHIVE.7z) and pass the
#                      directory that then contains the .img files:
#
#                          7z x "SCO Xenix 386 2.3.4q (3.5).7z"
#                          tools/xenix/build-image386.sh \
#                              -m "SCO Xenix 386 2.3.4q (3.5)" \
#                              -o xenix386-234.img -l xenix386-234
#
#   What comes out     OUTPUT, an ext2 image.  It holds SCO's software and
#                      is not to be committed either (*.img is ignored).
#
# Produces an ext2 image holding the installed system's files.  ext2 rather
# than a Xenix filesystem, for the reason build-image.sh gives: substrate
# reads it through its own VFS and runs the x.out binaries under the Xenix
# personality, so the container is substrate's business and only the file
# contents are Xenix's.
#
# WHAT THE MEDIA LOOKS LIKE
#   A Xenix/386 release is a directory of floppy images of two kinds, and
#   the script tells them apart by trying each as a tar archive:
#
#   filesystem floppies   the boot floppy (N1: /boot, /xenix, a first /etc)
#                         and, on 2.2.3, a separate root floppy (N2: /bin,
#                         /dev, /etc, the installer).  Xenix filesystems,
#                         read out whole with xenixfs.py.
#   tar floppies          everything else: the remaining Installation
#                         volumes, then Basic Utilities, Extended Utilities
#                         and Games.
#
#   They are laid down in the order the installer would: filesystem floppies
#   first, then the Installation tar volumes, then the rest by name -- so a
#   full program from a later volume replaces the cut-down one the
#   installation floppy carried to get that far.
#
#   ./tmp in a tar volume is the `custom` installer's scaffolding (_lbl
#   volume labels, perms manifests, init.* hooks) and is left out.
#
#   Nothing is serialized.  The files are as they come off the media;
#   brand(1) and the serial number the media was sold with are not used.
#
# A Xenix/386 system is not all 386 code: a large share of its utilities
# are 80286 or 8086 x.out binaries, which is why one personality runs all
# three.
#
# Usage: build-image386.sh -m MEDIA_DIR -o OUTPUT [-s SIZE_MB] [-l LABEL]

set -e

MEDIA=""
OUT=""
SIZE_MB=64
LABEL=xenix386
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

while [ $# -gt 0 ]; do
    case "$1" in
    -m|--media)  MEDIA=$2; shift 2 ;;
    -o|--output) OUT=$2; shift 2 ;;
    -s|--size)   SIZE_MB=$2; shift 2 ;;
    -l|--label)  LABEL=$2; shift 2 ;;
    -h|--help)   sed -n '2,59p' "$0"; exit 0 ;;
    *) echo "$0: unknown argument: $1" >&2; exit 64 ;;
    esac
done
[ -n "$MEDIA" ] && [ -n "$OUT" ] || { sed -n '59p' "$0" >&2; exit 64; }
[ -d "$MEDIA" ] || { echo "$0: no such directory: $MEDIA" >&2; exit 66; }

for tool in mke2fs fakeroot debugfs e2fsck tar python3; do
    command -v "$tool" >/dev/null || { echo "$0: need $tool" >&2; exit 65; }
done

STAGE=$(mktemp -d)
trap 'chmod -R u+rwX "$STAGE" 2>/dev/null; rm -rf "$STAGE"' EXIT INT TERM
ROOT=$STAGE/root
mkdir -p "$ROOT"

# Sort the floppies into the three groups, in name order within each.
FS=$STAGE/fs.list; INST=$STAGE/inst.list; REST=$STAGE/rest.list
: > "$FS"; : > "$INST"; : > "$REST"
find "$MEDIA" -maxdepth 1 -type f -name '*.img' | LC_ALL=C sort > "$STAGE/all.list"
[ -s "$STAGE/all.list" ] || { echo "$0: no *.img under $MEDIA" >&2; exit 66; }
while IFS= read -r img; do
    if tar tf "$img" >/dev/null 2>&1 && [ -n "$(tar tf "$img" 2>/dev/null | head -1)" ]; then
        case "$(basename "$img")" in
            Installation*|installation*) echo "$img" >> "$INST" ;;
            *)                           echo "$img" >> "$REST" ;;
        esac
    else
        echo "$img" >> "$FS"
    fi
done < "$STAGE/all.list"

# A later volume has to be able to add to, and replace files in, a
# directory an earlier one created without owner write permission.  Such
# directories are opened up as they turn up, and their real modes kept for
# the end; a mode recorded once is not recorded again after it was changed.
DIRMODES=$STAGE/dirmodes.txt
: > "$DIRMODES"
open_dirs() {
    find "$1" -type d ! -perm -u+rwx -printf '%m %P\n' 2>/dev/null |
    while read -r mode path; do
        [ "$1" = "$ROOT" ] && ! grep -q -x -F "d $mode $path" "$DIRMODES" &&
            ! grep -q " $path\$" "$DIRMODES" && echo "d $mode $path" >> "$DIRMODES"
        chmod u+rwx "$1/$path"
    done
}

echo "==> filesystem floppies"
n=0
while IFS= read -r img; do
    n=$((n + 1))
    if "$HERE/xenixfs.py" extract "$img" "$STAGE/fs$n" >/dev/null 2>&1; then
        echo "    $(basename "$img")"
        open_dirs "$ROOT"
        # Merge through tar, which preserves modes and replaces a file an
        # earlier floppy left read-only.  Unreadable files are made
        # readable in the scratch copy only long enough to be copied, and
        # get their modes back in the tree.
        find "$STAGE/fs$n" ! -type l ! -perm -u+r -printf '%m %P\n' > "$STAGE/fs$n.modes"
        chmod -R u+rX "$STAGE/fs$n"
        find "$STAGE/fs$n" -type d ! -perm -u+w -exec chmod u+w {} +
        ( cd "$STAGE/fs$n" && tar cf - . ) | ( cd "$ROOT" && tar xf - )
        while read -r mode path; do
            [ -n "$path" ] && chmod "$mode" "$ROOT/$path"
        done < "$STAGE/fs$n.modes"
    else
        echo "    $(basename "$img"): neither a tar archive nor a Xenix filesystem; skipped" >&2
    fi
done < "$FS"

untar() {
    while IFS= read -r img; do
        echo "    $(basename "$img")"
        open_dirs "$ROOT"
        ( cd "$ROOT" && tar xf "$img" --exclude='./tmp' --exclude='./tmp/*' ) \
            2>/dev/null || true
    done < "$1"
}
echo "==> Installation volumes"
untar "$INST"
echo "==> the rest of the distribution"
untar "$REST"

# 2.3.4 ships most of its files compress(1)ed, under their final names --
# /bin/ls on the floppy is LZW data, and `custom` runs each through
# uncompress as it installs it.  Do the same: a file that starts with the
# compress magic (1f 9d) is replaced by what it expands to, keeping its mode.
echo "==> expanding compress(1)ed files"
open_dirs "$ROOT"
nz=0
find "$ROOT" -type f -size +2c > "$STAGE/files.list"
while IFS= read -r f; do
    [ -r "$f" ] || continue
    [ "$(dd if="$f" bs=1 count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 1f9d ] || continue
    if gzip -dc < "$f" > "$STAGE/z.tmp" 2>/dev/null; then
        mode=$(stat -c %a "$f")
        chmod u+w "$f"
        cat "$STAGE/z.tmp" > "$f"
        chmod "$mode" "$f"
        nz=$((nz + 1))
    else
        echo "    could not expand ${f#$ROOT}" >&2
    fi
done < "$STAGE/files.list"
echo "    $nz expanded"

mkdir -p "$ROOT/usr/tmp" "$ROOT/tmp"
chmod 1777 "$ROOT/tmp" "$ROOT/usr/tmp"

# Files the build user cannot read (Xenix ships some execute-only), and
# directories it cannot enter: staged readable so mke2fs can copy them,
# their real modes restored inside the image.
echo "==> staging unreadable files readable so they can be copied"
MODES=$STAGE/modes.txt
open_dirs "$ROOT"
cp "$DIRMODES" "$MODES"
find "$ROOT" -type f ! -perm -u+r -printf 'f %m %P\n' >> "$MODES"
while read -r kind mode path; do
    [ -n "$path" ] || continue
    if [ "$kind" = d ]; then chmod u+rwx "$ROOT/$path"; else chmod u+r "$ROOT/$path"; fi
done < "$MODES"
echo "    $(wc -l < "$MODES") staged readable"

files=$(find "$ROOT" -type f | wc -l)
inodes=$(( $(find "$ROOT" | wc -l) + 2048 ))

echo "==> building $OUT (${SIZE_MB}M, ext2, 1024-byte blocks)"
rm -f "$OUT"
fakeroot -- sh -c "
    chown -R 0:0 '$ROOT' &&
    mke2fs -q -F -b 1024 -t ext2 -L '$LABEL' -N $inodes \
        -O ext_attr,resize_inode,dir_index,filetype,sparse_super,large_file \
        -d '$ROOT' '$OUT' $((SIZE_MB * 1024))
"

if [ -s "$MODES" ]; then
    echo "==> restoring the real modes inside the image"
    CMDS=$STAGE/debugfs.cmd
    : > "$CMDS"
    while read -r kind mode path; do
        [ -n "$path" ] || continue
        # debugfs wants the full mode word: the file type and the bits.
        if [ "$kind" = d ]; then t=040; else t=0100; fi
        printf 'sif "/%s" mode %s%s\n' "$path" "$t" "$(printf '%03d' "$mode")" >> "$CMDS"
    done < "$MODES"
    debugfs -w -f "$CMDS" "$OUT" >/dev/null 2>&1
fi

echo "==> verifying"
e2fsck -fp "$OUT" >/dev/null 2>&1 || {
    rc=$?
    [ "$rc" -le 1 ] || { echo "$0: e2fsck reported $rc" >&2; exit 1; }
}
echo "    $files files, $(du -h "$OUT" | cut -f1) on disk, fsck clean"
echo "    shell: $(debugfs -R 'stat /bin/sh' "$OUT" 2>/dev/null | grep -o 'Size: [0-9]*' | head -1)"
echo "done: $OUT"
