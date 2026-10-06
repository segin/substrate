#!/bin/bash
#
# build-image.sh - build a /perso/svr3 disk image from AT&T UNIX System
# V/386 Release 3 distribution media.
#
# REQUIRES EXTERNAL MEDIA.  Nothing this script installs is in the
# repository, and it cannot run without files you supply: the floppy and
# tape images of a System V/386 Release 3 distribution.  It builds no part
# of substrate and no other script or test depends on it.
#
#   What to provide    one or more directories unpacked from the archives
#                      bitsavers keeps under bits/ATT/SYSV_386/ -- e.g.
#                      SYSV_386_3.2.3_cartridge_16user.zip (boot floppies
#                      and the cartridge tape), SYSV_386_3.2.3_1.44mb_2user.zip
#                      (the same release on floppies), SYSV_386_3.2_SDS_4.1.5.zip
#                      (the C Software Development Set), or
#                      SYSV_386_3.1_1.2mb_disk1_missing.zip.
#   How                unzip them and pass each directory with -m.  The
#                      first is the system; later ones are added to it:
#
#                          unzip SYSV_386_3.2.3_cartridge_16user.zip
#                          unzip SYSV_386_3.2_SDS_4.1.5.zip
#                          tools/svr3/build-image.sh \
#                              -m SYSV_386_3.2.3_cartridge_16user \
#                              -m SYSV_386_3.2_SDS_4.1.5 -o svr3-323.img
#
#   What comes out     OUTPUT, an ext2 image.  It holds AT&T's software and
#                      is not to be committed either (*.img is ignored).
#
# The image is ext2 rather than an s5 filesystem because substrate reads it
# through its own VFS and runs the COFF binaries under the SVR3
# personality: the container is substrate's business, only the file
# contents are System V's.  Mount it at /perso/svr3.
#
# WHAT THE MEDIA LOOKS LIKE
#   Floppies are archived as ImageDisk files (*.IMD), which imd2raw.py turns
#   into sector images.  The first cylinder of each is a label; after it
#   comes one of
#
#   an s5 filesystem     the boot floppy -- the only place a release ships
#                        /bin/sh and /shlib/libc_s -- and the Remote
#                        Terminal floppy.  Read with ../svr4/s5fs.py.
#   a cpio archive       old ASCII format (magic 070707).  A base system
#                        floppy is an archive of its own; a package is one
#                        archive that runs on across as many floppies as it
#                        needs, so a floppy whose data does not begin with
#                        a header continues the one before it.
#
#   The cartridge tape (tape/burst/NNNNN, one file per tape file) is the
#   same archives without the floppies: the base system and the packages.
#   Loose *.cpio files in the media directory are taken as archives too.
#
#   An archive with Name, Files, Install and Remove in it is a package for
#   installpkg(1).  Its Install script is not run; its files are put where
#   its Files list says they go, which for most packages is all the script
#   does.  Anything else is laid down as it is.
#
#   Symbolic links to absolute paths are made relative, so that they stay
#   inside /perso/svr3.
#
# Usage: build-image.sh -m MEDIA_DIR [-m MEDIA_DIR ...] -o OUTPUT
#                       [-s SIZE_MB] [-l LABEL]

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
S5FS=$HERE/../svr4/s5fs.py
MEDIA=()
OUT=
SIZE_MB=128
LABEL=svr3

usage() {
    sed -n '2,/^# Usage/p' "$0" | sed 's/^# \{0,1\}//'
    sed -n '/^# Usage/,/^$/p' "$0" | sed 's/^# \{0,1\}//' | tail -n +2
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case $1 in
        -m) MEDIA+=("$2"); shift 2 ;;
        -o) OUT=$2; shift 2 ;;
        -s) SIZE_MB=$2; shift 2 ;;
        -l) LABEL=$2; shift 2 ;;
        -h|--help) usage 0 ;;
        *) echo "build-image.sh: unknown argument: $1" >&2; usage 2 ;;
    esac
done

if [ ${#MEDIA[@]} -eq 0 ] || [ -z "$OUT" ]; then
    echo "build-image.sh: -m MEDIA_DIR and -o OUTPUT are required." >&2
    echo "This script needs distribution media that is not in the" >&2
    echo "repository; run it with --help for what to obtain." >&2
    exit 2
fi
for d in "${MEDIA[@]}"; do
    [ -d "$d" ] || { echo "build-image.sh: no such directory: $d" >&2; exit 1; }
done
for tool in mke2fs fakeroot debugfs e2fsck python3 cpio realpath; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-image.sh: $tool is required" >&2; exit 1; }
done

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/svr3img.XXXXXX")
trap 'chmod -R u+rwx "$STAGE" 2>/dev/null; rm -rf "$STAGE"' EXIT
ROOT=$STAGE/root
MODES=$STAGE/modes
mkdir -p "$ROOT"
: > "$MODES"

# The COUNT bytes at OFFSET of FILE, as text.
magic() {
    dd if="$1" bs=1 skip="$2" count="$3" 2>/dev/null | tr -d '\0'
}

# Unpack the cpio archive on stdin into DIR, noting each member's mode in
# DIR/.modes and leaving everything readable.
unpack() {
    mkdir -p "$1"
    (cd "$1" && cpio -idmu --quiet --no-preserve-owner 2>/dev/null) || true
    (cd "$1" && find . -mindepth 1 ! -type l -printf '%m %P\n') > "$1/.modes"
    chmod -R u+rwX "$1"
}

# Lay the tree SRC down over the root, with its modes.
lay_down() {
    (cd "$1" && find . -mindepth 1 ! -name .modes -print0 |
        cpio -pdmu0 --quiet "$ROOT" 2>/dev/null) || true
    cat "$1/.modes" >> "$MODES"
}

# Install the unpacked package in DIR: its directories as they are, and
# each loose file where the Files list says a file of that name goes.
install_pkg() {
    local p=$1 name n=0 dest f
    name=$(head -1 "$p/Name" 2>/dev/null | tr -d '\r' | cut -c1-50)
    for f in "$p"/*; do
        [ -d "$f" ] || continue
        mkdir -p "$STAGE/tree"
        cp -a "$f" "$STAGE/tree/"
    done
    if [ -d "$STAGE/tree" ]; then
        grep -v '^[0-7]* [^/]*$' "$p/.modes" > "$STAGE/tree/.modes" || true
        n=$(find "$STAGE/tree" -type f ! -name .modes | wc -l)
        lay_down "$STAGE/tree"
        rm -rf "$STAGE/tree"
    fi
    while IFS= read -r dest; do
        dest=${dest#.}; dest=${dest#/}
        [ -n "$dest" ] || continue
        f=$p/$(basename "$dest")
        [ -f "$f" ] && [ ! -e "$p/$dest" ] || continue
        # A Files entry can name a directory the file goes into; where
        # that cannot be told from a file's own path, leave it out.
        mkdir -p "$ROOT/$(dirname "$dest")" 2>/dev/null || continue
        [ ! -d "$ROOT/$dest" ] || continue
        cp -f "$f" "$ROOT/$dest"
        echo "$(awk -v n="$(basename "$dest")" '$2 == n { print $1 }' "$p/.modes" | head -1) $dest" >> "$MODES"
        n=$((n + 1))
    done < <(tr -d '\r' < "$p/Files" 2>/dev/null)
    printf '    package  %-44s %d file(s)\n' "${name:-?}" "$n"
}

# An unpacked archive is a package if installpkg's files are in it.
place() {
    if [ -f "$1/Name" ] && [ -f "$1/Files" ]; then
        install_pkg "$1"
    else
        lay_down "$1"
        printf '    files    %-44s %d file(s)\n' "$2" \
            "$(find "$1" -type f ! -name .modes | wc -l)"
    fi
    rm -rf "$1"
}

for dir in "${MEDIA[@]}"; do
    echo "==> $(basename "$dir")"
    RAW=$STAGE/raw
    rm -rf "$RAW"; mkdir -p "$RAW"
    while IFS= read -r -d '' f; do
        python3 "$HERE/imd2raw.py" "$f" \
            "$RAW/$(basename "${f%.*}" | tr 'a-z' 'A-Z').img" >/dev/null
    done < <(find "$dir" -maxdepth 1 -iname '*.imd' -print0 | sort -z)

    # Filesystem floppies first: the boot floppy's shell and libraries are
    # what everything after it needs, and a fuller base replaces its tools.
    cpios=()
    for f in "$RAW"/*.img; do
        [ -f "$f" ] || continue
        off=$(( $(stat -c %s "$f") / 80 ))        # one cylinder
        if python3 "$S5FS" ls "$f" / -o "$off" >/dev/null 2>&1; then
            printf '    s5 fs    %-44s ' "$(basename "$f" .img)"
            python3 "$S5FS" extract "$f" "$STAGE/fs" -o "$off"
            # Only a floppy with a system on it; the Remote Terminal one
            # is an installer of its own.
            if [ -d "$STAGE/fs/bin" ]; then
                lay_down "$STAGE/fs"
            fi
            rm -rf "$STAGE/fs"
        else
            cpios+=("$f")
        fi
    done

    # Then the archives, each with the floppies that continue it.
    i=0
    while [ $i -lt ${#cpios[@]} ]; do
        f=${cpios[$i]}
        off=$(( $(stat -c %s "$f") / 80 ))
        i=$((i + 1))
        if [ "$(magic "$f" "$off" 6)" != "070707" ]; then
            echo "    (skipping $(basename "$f" .img): continues a floppy that is not here)"
            continue
        fi
        vols=("$f")
        while [ $i -lt ${#cpios[@]} ]; do
            g=${cpios[$i]}
            [ "$(magic "$g" $(( $(stat -c %s "$g") / 80 )) 6)" != "070707" ] || break
            vols+=("$g"); i=$((i + 1))
        done
        for v in "${vols[@]}"; do
            dd if="$v" bs=512 skip=$(( $(stat -c %s "$v") / 80 / 512 )) 2>/dev/null
        done | unpack "$STAGE/ar"
        place "$STAGE/ar" "$(basename "$f" .img)"
    done

    # The tape, and any archives lying loose.
    while IFS= read -r -d '' f; do
        [ "$(magic "$f" 0 6)" = "070707" ] || continue
        unpack "$STAGE/ar" < "$f"
        place "$STAGE/ar" "$(basename "$f")"
    done < <(find "$dir" \( -path '*/tape/burst/*' ! -name '*.txt' -o -name '*.cpio' \) \
                 -type f -print0 | sort -z)
done

# Absolute symbolic links would name substrate's own files.
while IFS= read -r -d '' l; do
    t=$(readlink "$l")
    case $t in
        /*) ln -sfn "$(realpath -m --relative-to="$(dirname "$l")" "$ROOT$t")" "$l" ;;
    esac
done < <(find "$ROOT" -type l -print0)

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

# Modes are set in the image: many files are unreadable to their owner and
# were staged readable so they could be copied.  The last one recorded for
# a path is the one from the volume that supplied the file.
CMDS=$STAGE/debugfs.cmd
: > "$CMDS"
tac "$MODES" | awk 'NF == 2 && !seen[$2]++' | while read -r mode path; do
    [ -e "$ROOT/$path" ] || continue
    [ -L "$ROOT/$path" ] && continue
    if [ -d "$ROOT/$path" ]; then t=040000; else t=0100000; fi
    printf 'sif "/%s" mode 0%o\n' "$path" $(( t | 0$mode )) >> "$CMDS"
done
debugfs -w -f "$CMDS" "$OUT" >/dev/null 2>&1

e2fsck -fn "$OUT" 2>&1 | tail -1 | sed 's/^/    /'
echo "    shell:  $(debugfs -R 'stat /bin/sh' "$OUT" 2>/dev/null | grep -o 'Size: [0-9]*' | head -1)"
echo "    libc_s: $(debugfs -R 'stat /shlib/libc_s' "$OUT" 2>/dev/null | grep -o 'Size: [0-9]*' | head -1)"
echo "Mount at /perso/svr3:  mount <device> /perso/svr3 ext2"
