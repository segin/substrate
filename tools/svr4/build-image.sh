#!/bin/bash
#
# build-image.sh - build a /perso/svr4 disk image from the distribution
# floppies of UNIX System V/386 Release 4.0.
#
# REQUIRES EXTERNAL MEDIA.  Nothing this script installs is in the
# repository, and it cannot run without files you supply: the floppy images
# of an i386 System V Release 4.0 distribution.  It builds no part of
# substrate and no other script or test depends on it.
#
#   What to provide    a directory holding the release's floppy images as
#                      *.img files, one per floppy, named so that they sort
#                      in distribution order -- as archived by WinWorld in
#                      "Intel Unix System V R4.0 V2.0 (1990) (5.25-1.2mb).7z"
#                      (58 images, "Disk 01 - Boot - ..." onward).
#   How                unpack the archive (7z x ARCHIVE.7z) and pass the
#                      directory that then contains the .img files:
#
#                          7z x "Intel Unix System V R4.0 V2.0 (1990) (5.25-1.2mb).7z"
#                          tools/svr4/build-image.sh \
#                              -m "Intel Unix System V R4.0 V2.0 (1990) (5.25-1.2mb)" \
#                              -o svr4.img
#
#   What comes out     OUTPUT, an ext2 image.  It holds the vendor's
#                      software and is not to be committed either (*.img is
#                      ignored).
#
# The image is ext2 rather than an s5 filesystem because substrate reads it
# through its own VFS and runs the ELF binaries under the SVR4 personality:
# the container is substrate's business, only the file contents are
# System V's.  Mount it at /perso/svr4.
#
#   Or                 the factory tape of Dell UNIX SVR4 Issue 2.2, the
#                      DellSVR4/Factory directory of DellSVR4v22.tar.lz
#                      from tenox.pdp-11.net/os/dellunix/ (see README.md):
#
#                          bsdtar xf DellSVR4v22.tar.lz
#                          tools/svr4/build-image.sh -m DellSVR4/Factory \
#                              -o dellunix.img -s 640 -l dellunix
#
# WHAT THE MEDIA LOOKS LIKE
#   Every regular file in MEDIA_DIR is a volume.  Three kinds of floppy,
#   told apart by content, not by name, and a fourth that a tape has:
#
#   base system archive   a cpio archive, from byte 0, of a whole root
#                         (Dell's file1).  Laid down after the Foundation
#                         Set and before the packages.
#   s5 filesystems        the boot floppies.  Several first boot floppies
#                         ship, one per disk controller, differing only in
#                         drivers; the first is used.  With the second boot
#                         floppy they are the only place the release ships
#                         /sbin/sh, /sbin/init and /usr/lib/libc.so.1.
#                         Read with s5fs.py.
#   Foundation Set        a volume label and then one cpio archive per
#                         floppy, starting at byte 15360: the base system.
#   package datastreams   everything else, in pkgadd(1M) format: a header
#                         block and then cpio archives.  A package's files
#                         are under reloc/ or root/ (reloc.N/, root.N/ on
#                         its Nth floppy); pkginfo, pkgmap and install/ are
#                         the package tools' own and are left out.  Both
#                         read with svr4cpio.py.
#
#   They are laid down in that order, so the full program from the base
#   system replaces the cut-down one a boot floppy carried.
#
#   Nothing is installed in the pkgadd sense: no install scripts are run,
#   and pkgmap's devices and ownership are not applied.  The links an
#   installation makes are made -- pkgmap's, and the `ln` commands of the
#   boot floppy's install scripts -- and files that ship compress(1)ed are
#   expanded, as the scripts would have done.
#
# Usage: build-image.sh -m MEDIA_DIR -o OUTPUT [-s SIZE_MB] [-l LABEL]
#                       [--base-only]

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
MEDIA=
OUT=
SIZE_MB=256
LABEL=svr4
BASE_ONLY=0

usage() {
    sed -n '2,/^# Usage/p' "$0" | sed 's/^# \{0,1\}//'
    sed -n '/^# Usage/,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case $1 in
        -m) MEDIA=$2; shift 2 ;;
        -o) OUT=$2; shift 2 ;;
        -s) SIZE_MB=$2; shift 2 ;;
        -l) LABEL=$2; shift 2 ;;
        --base-only) BASE_ONLY=1; shift ;;
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
if [ ! -d "$MEDIA" ] || [ -z "$(find "$MEDIA" -maxdepth 1 -type f -print -quit)" ]; then
    echo "build-image.sh: no media files in '$MEDIA'." >&2
    echo "Run with --help for what to provide." >&2
    exit 1
fi
for tool in mke2fs fakeroot debugfs e2fsck python3 gzip; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-image.sh: $tool is required" >&2; exit 1; }
done

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/svr4img.XXXXXX")
trap 'chmod -R u+rwx "$STAGE" 2>/dev/null; rm -rf "$STAGE"' EXIT
ROOT=$STAGE/root
LINKS=$STAGE/links
mkdir -p "$ROOT"
: > "$LINKS"

# The COUNT bytes at OFFSET of FILE, as text (NULs dropped).
magic() {
    dd if="$1" bs=1 skip="$2" count="$3" 2>/dev/null | tr -d '\0'
}

# Sort the volumes into their kinds.
s5=() fnd=() pkg=() base=()
for f in "$MEDIA"/*; do
    [ -f "$f" ] || continue
    if [ "$(magic "$f" 0 20)" = "# PaCkAgE DaTaStReAm" ]; then
        pkg+=("$f")
    elif [ "$(magic "$f" 15360 6)" = "070701" ]; then
        fnd+=("$f")
    elif [ "$(magic "$f" 0 6)" = "070701" ]; then
        # A package's second or later floppy, or a whole root.
        case $(python3 "$HERE/svr4cpio.py" list "$f" | sed -n '1s/.* //p') in
            reloc*|root*|pkginfo|pkgmap|install*) pkg+=("$f") ;;
            .|dev|dev/*|etc|etc/*|sbin|sbin/*|usr|usr/*|.[a-z]*) base+=("$f") ;;
            *) echo "    (skipping $(basename "$f"): a cpio archive that is not a root or a package)" ;;
        esac
    elif python3 "$HERE/s5fs.py" ls "$f" / >/dev/null 2>&1; then
        s5+=("$f")
    else
        echo "    (skipping $(basename "$f"): not a recognised volume)"
    fi
done

# The first boot floppy comes once per disk controller; one is enough.
# They are the ones with a kernel on them.
boot1=
rest_s5=()
for f in "${s5[@]}"; do
    if python3 "$HERE/s5fs.py" ls "$f" / 2>/dev/null | grep -q ' unix$'; then
        [ -z "$boot1" ] && boot1=$f
    else
        rest_s5+=("$f")
    fi
done

echo "==> boot floppies (s5 filesystems)"
for f in ${boot1:+"$boot1"} "${rest_s5[@]}"; do
    printf '    %-58s ' "$(basename "$f" | cut -c1-58)"
    python3 "$HERE/s5fs.py" extract "$f" "$ROOT"
done

echo "==> Foundation Set (${#fnd[@]} floppies)"
for f in "${fnd[@]}"; do
    printf '    %-58s ' "$(basename "$f" | cut -c1-58)"
    python3 "$HERE/svr4cpio.py" extract "$f" "$ROOT"
done

if [ ${#base[@]} -gt 0 ]; then
    echo "==> base system archives (${#base[@]})"
    for f in "${base[@]}"; do
        printf '    %-58s ' "$(basename "$f" | cut -c1-58)"
        python3 "$HERE/svr4cpio.py" extract "$f" "$ROOT"
    done
fi

if [ "$BASE_ONLY" = 0 ]; then
    echo "==> packages (${#pkg[@]} volumes)"
    for f in "${pkg[@]}"; do
        P=$STAGE/pkg
        rm -rf "$P"
        python3 "$HERE/svr4cpio.py" extract "$f" "$P" >/dev/null
        n=0
        # pkgmap's links: "part l class path=target", s for symbolic.  A
        # datastream of several packages has a copy of each one's pkgmap
        # under the package's name.
        for map in "$P/pkgmap" "$P"/*/pkgmap; do
            [ -f "$map" ] || continue
            awk '($2 == "l" || $2 == "s") && $4 ~ /=/ && $4 !~ /\$/ {
                     split($4, a, "="); print $2, a[1], a[2] }' \
                "$map" >> "$LINKS"
        done
        # reloc/, root/, reloc.N/ and root.N/ hold the files; a path
        # component that is a $PARAMETER is one pkgadd would have asked
        # about, and those files are left out.
        for top in "$P"/reloc "$P"/reloc.* "$P"/root "$P"/root.*; do
            [ -d "$top" ] || continue
            while IFS= read -r -d '' src; do
                rel=${src#"$top"/}
                case $rel in *\$*) continue ;; esac
                mkdir -p "$ROOT/$(dirname "$rel")"
                rm -f "$ROOT/$rel"
                cp -P "$src" "$ROOT/$rel"
                n=$((n + 1))
            done < <(find "$top" \( -type f -o -type l \) -print0)
            # Carry the modes the archive recorded.
            t=$(basename "$top")
            grep " $t/" "$P/.modes" 2>/dev/null | grep -v '\$' |
                sed "s# $t/# #" >> "$ROOT/.modes" || true
        done
        # pkgmap has the mode a file is installed with, which the archive
        # need not: "part f|d class path mode owner group ...".  It comes
        # after the archive's, so it is the one applied.
        for map in "$P/pkgmap" "$P"/*/pkgmap; do
            [ -f "$map" ] || continue
            awk '($2 == "f" || $2 == "d" || $2 == "e" || $2 == "v" || $2 == "x") &&
                 $5 ~ /^[0-7]+$/ && $4 !~ /\$/ {
                     sub(/^\//, "", $4); print $5, $4 }' "$map" >> "$ROOT/.modes"
        done
        printf '    %-58s %d file(s)\n' "$(basename "$f" | cut -c1-58)" "$n"
    done
fi

# The links an installation makes.  The base system's are `ln` commands in
# the boot floppy's install scripts; the packages' came from pkgmap above.
# Every link is made relative: the image is mounted under /perso/svr4, and
# a symbolic link to /sbin/grep would otherwise name substrate's own.
echo "==> links"
for s in "$ROOT"/hd.instl.* "$ROOT"/flop.instl.*; do
    [ -f "$s" ] || continue
    sed -n 's/^[[:space:]]*ln[[:space:]]\{1,\}\(-s[[:space:]]\{1,\}\)\{0,1\}\(\/[^ $]*\)[[:space:]]\{1,\}\(\/[^ $]*\).*/\1 \3 \2/p' "$s" |
        while read -r a b c; do
            if [ "$a" = "-s" ]; then echo "s $b $c"; else echo "l $a $b"; fi
        done >> "$LINKS"
done
# /bin is /usr/bin on System V Release 4; the installer's prototype makes
# it, and no floppy carries it.
[ -e "$ROOT/bin" ] || echo "s /bin /usr/bin" >> "$LINKS"
made=0
while read -r kind path target; do
    path=${path#/}
    case $target in /*) target=${target#/} ;; *) [ "$kind" = s ] && target=$(dirname "$path")/$target ;; esac
    [ -e "$ROOT/$path" ] || [ -L "$ROOT/$path" ] && continue
    [ -e "$ROOT/$target" ] || continue
    mkdir -p "$ROOT/$(dirname "$path")"
    if [ "$kind" = l ] && [ ! -d "$ROOT/$target" ]; then
        ln "$ROOT/$target" "$ROOT/$path"
    else
        ln -s "$(realpath -m --relative-to="$ROOT/$(dirname "$path")" "$ROOT/$target")" "$ROOT/$path"
    fi
    made=$((made + 1))
done < "$LINKS"
echo "    $made link(s)"

# A symbolic link to an absolute path would name substrate's own file, not
# the one under /perso/svr4.
while IFS= read -r -d '' l; do
    t=$(readlink "$l")
    case $t in
        /*) ln -sfn "$(realpath -m --relative-to="$(dirname "$l")" "$ROOT$t")" "$l" ;;
    esac
done < <(find "$ROOT" -type l -print0)

echo "==> expanding compress(1)ed files"
z=0
while IFS= read -r -d '' f; do
    if [ "$(head -c 2 "$f" | od -An -tx1 | tr -d ' ')" = "1f9d" ]; then
        case $f in *.Z) continue ;; esac
        if gzip -dc < "$f" > "$STAGE/z.tmp" 2>/dev/null; then
            cat "$STAGE/z.tmp" > "$f"
            z=$((z + 1))
        fi
    fi
done < <(find "$ROOT" -type f -print0)
echo "    $z file(s)"

# Many files are unreadable or unwritable to their owner.  They are staged
# readable so mke2fs can copy them, and their modes set in the image.
MODES=$STAGE/modes
mv "$ROOT/.modes" "$MODES"
chmod -R u+rwX "$ROOT"

files=$(find "$ROOT" -type f | wc -l)
inodes=$(( $(find "$ROOT" | wc -l) + 2048 ))
echo "==> building $OUT (${SIZE_MB} MB, $files files)"
rm -f "$OUT"
fakeroot -- sh -c "
    chown -R 0:0 '$ROOT'
    mke2fs -q -F -b 1024 -t ext2 -L '$LABEL' -N $inodes \
        -d '$ROOT' '$OUT' ${SIZE_MB}M
"

CMDS=$STAGE/debugfs.cmd
: > "$CMDS"
# The last mode recorded for a path is the one from the volume that
# supplied the file.
tac "$MODES" | awk '!seen[$2]++' | while read -r mode path; do
    [ -e "$ROOT/$path" ] || continue
    [ -L "$ROOT/$path" ] && continue
    if [ -d "$ROOT/$path" ]; then t=040000; else t=0100000; fi
    printf 'sif "/%s" mode 0%o\n' "$path" $(( t | 0$mode )) >> "$CMDS"
done
debugfs -w -f "$CMDS" "$OUT" >/dev/null 2>&1

e2fsck -fn "$OUT" 2>&1 | tail -1 | sed 's/^/    /'
echo "    shell: $(debugfs -R 'stat /sbin/sh' "$OUT" 2>/dev/null | grep -o 'Size: [0-9]*' | head -1)"
echo "    libc:  $(debugfs -R 'stat /usr/lib/libc.so.1' "$OUT" 2>/dev/null | grep -o 'Size: [0-9]*' | head -1)"
echo "Mount at /perso/svr4:  mount <device> /perso/svr4 ext2"
