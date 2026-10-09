#!/bin/bash
#
# build-image-isc.sh - build a /perso/svr3 disk image from the distribution
# floppies of INTERACTIVE UNIX System V/386 Release 3.2.
#
# REQUIRES EXTERNAL MEDIA.  Nothing this script installs is in the
# repository, and it cannot run without files you supply: the floppy images
# of an INTERACTIVE UNIX release.  It builds no part of substrate and no
# other script or test depends on it.
#
#   What to provide    a directory holding the release's floppies as raw
#                      *.img files, one per floppy -- e.g. the contents of
#                      interactive-unix30.tar.lz (INTERACTIVE UNIX 3.0, 49
#                      floppies) from the Tenox archive,
#                      tenox.pdp-11.net/os/interactiveunix/3.x/.
#   How                unpack the archive and pass the directory that then
#                      contains the .img files:
#
#                          bsdtar xf interactive-unix30.tar.lz
#                          tools/svr3/build-image-isc.sh \
#                              -m interactive-unix-30 -o iu30.img
#
#   What comes out     OUTPUT, an ext2 image.  It holds the vendor's
#                      software and is not to be committed either (*.img is
#                      ignored).
#
# INTERACTIVE UNIX is System V Release 3.2 for the 386 with a great deal
# added, and its programs are Release 3 COFF binaries, so the image is for
# the SVR3 personality: mount it at /perso/svr3.  It is ext2 because
# substrate reads it through its own VFS; only the file contents are
# INTERACTIVE's.
#
# WHAT THE MEDIA LOOKS LIKE
#   Every floppy is an s5 filesystem (read with ../svr4/s5fs.py):
#
#   the Boot floppy       the kernel, after a one-cylinder label.
#   the Install floppy    a small root: /bin/sh, /shlib/libc_s, the tools
#                         the installation runs on.  Laid down first.
#   everything else       one or more subsets, each a directory:
#                             SUBSET/new/...       its files, at the paths
#                                                  they install to, each
#                                                  compress(1)ed as NAME.Z
#                             SUBSET/install/link  a script of the renames
#                                                  and links to make after
#                         A subset continues over several floppies under
#                         the same name.
#
#   The files are expanded and put in place, and the `mv` and `ln` lines of
#   each subset's link script are carried out (the first restore names too
#   long for the s5 floppy; device links are left out).  No other install
#   script is run.  Symbolic links to absolute paths are made relative, so
#   that they stay inside /perso/svr3.
#
#   Nothing is serialized: the serial number an archive may carry is not
#   used.
#
# VENIX/386
#   VenturCom's Venix/386 3.2.4 (the WinWorld archive "Venix-386 3.2.4
#   (1991) (5.25-1.2mb)", nineteen raw *.img floppies) is the same Release
#   3.2 packaged half this way: its optional sets (file1, fs1, kernel1-4,
#   net1-2, streams1, terminal1) are subset floppies as above, and its
#   base system (core1-6) is one cpio archive, old ASCII headers, running
#   over six floppies behind a one-cylinder label on each -- which is how
#   AT&T's own floppies are, see build-image.sh.  This script reads both:
#
#       tools/svr3/build-image-isc.sh \
#           -m 'Venix-386 3.2.4 (1991) (5.25-1.2mb)' -o venix386.img -l venix386
#
# Usage: build-image-isc.sh -m MEDIA_DIR -o OUTPUT [-s SIZE_MB] [-l LABEL]

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
S5FS=$HERE/../svr4/s5fs.py
MEDIA=
OUT=
SIZE_MB=256
LABEL=iu

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
        *) echo "build-image-isc.sh: unknown argument: $1" >&2; usage 2 ;;
    esac
done

if [ -z "$MEDIA" ] || [ -z "$OUT" ]; then
    echo "build-image-isc.sh: -m MEDIA_DIR and -o OUTPUT are required." >&2
    echo "This script needs distribution media that is not in the" >&2
    echo "repository; run it with --help for what to obtain." >&2
    exit 2
fi
if [ ! -d "$MEDIA" ] || ! ls "$MEDIA"/*.img >/dev/null 2>&1; then
    echo "build-image-isc.sh: no *.img floppy images in '$MEDIA'." >&2
    echo "Run with --help for what to provide." >&2
    exit 1
fi
for tool in mke2fs fakeroot debugfs e2fsck python3 gzip realpath; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-image-isc.sh: $tool is required" >&2; exit 1; }
done

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/iscimg.XXXXXX")
trap 'chmod -R u+rwx "$STAGE" 2>/dev/null; rm -rf "$STAGE"' EXIT
ROOT=$STAGE/root
MODES=$STAGE/modes
LINKS=$STAGE/links
mkdir -p "$ROOT"
: > "$MODES"
: > "$LINKS"

# The Install floppy is the one with a shell on it; it goes first, so that
# the subsets' fuller programs replace its cut-down ones.
first=() rest=()
for f in "$MEDIA"/*.img; do
    if python3 "$S5FS" ls "$f" /bin 2>/dev/null | grep -q ' sh$'; then
        first+=("$f")
    else
        rest+=("$f")
    fi
done

# Floppies that are a cpio archive behind a one-cylinder label: Venix/386's
# Core set.  The archive starts on the floppy whose data begins with a
# header and runs on over the ones after it, which are neither that nor a
# filesystem.  It is the base system and goes down before anything else.
cyl_of() { echo $(( $(stat -c %s "$1") / 80 )); }
starts_cpio() {
    [ "$(dd if="$1" bs=1 skip="$(cyl_of "$1")" count=6 2>/dev/null | tr -d '\0')" = 070707 ]
}
is_s5() { python3 "$S5FS" ls "$1" / >/dev/null 2>&1; }
vols=() cpio_floppies=" "
flush_cpio() {
    [ ${#vols[@]} -gt 0 ] || return 0
    local v AR=$STAGE/ar
    rm -rf "$AR"; mkdir -p "$AR"
    for v in "${vols[@]}"; do
        dd if="$v" bs=512 skip=$(( $(cyl_of "$v") / 512 )) 2>/dev/null
    done | (cd "$AR" && cpio -idmu --quiet --no-preserve-owner 2>/dev/null) || true
    (cd "$AR" && find . -mindepth 1 ! -type l -printf '%m %P\n') >> "$MODES"
    chmod -R u+rwX "$AR"
    printf '    %-28s %d file(s), cpio over %d floppies\n' \
        "$(basename "${vols[0]}" .img)" \
        "$(find "$AR" -type f | wc -l)" ${#vols[@]}
    cp -a "$AR/." "$ROOT/"
    rm -rf "$AR"
    vols=()
}
for f in "$MEDIA"/*.img; do
    if starts_cpio "$f"; then
        flush_cpio
        vols=("$f"); cpio_floppies="$cpio_floppies$f "
    elif [ ${#vols[@]} -gt 0 ] && ! is_s5 "$f"; then
        vols+=("$f"); cpio_floppies="$cpio_floppies$f "
    else
        flush_cpio
    fi
done
flush_cpio

subsets=0
for f in "${first[@]}" "${rest[@]}"; do
    case $cpio_floppies in *" $f "*) continue ;; esac
    FS=$STAGE/fs
    rm -rf "$FS"
    if ! python3 "$S5FS" extract "$f" "$FS" >/dev/null 2>&1; then
        echo "    (skipping $(basename "$f"): not an s5 filesystem)"
        continue
    fi
    chmod -R u+rwX "$FS"
    n=0
    if [ -d "$FS/bin" ] || [ -f "$FS/unix" ]; then
        # A root of its own: the Install or the Boot floppy.
        rm -f "$FS"/INSTALL* "$FS/.profile"
        n=$(find "$FS" -type f ! -name .modes | wc -l)
        grep -v ' INSTALL' "$FS/.modes" >> "$MODES" || true
        rm -f "$FS/.modes"
        cp -a "$FS/." "$ROOT/"
    else
        for sub in "$FS"/*/; do
            [ -d "$sub/new" ] || continue
            name=$(basename "$sub")
            n=$((n + $(find "$sub/new" -type f | wc -l)))
            cp -a "$sub/new/." "$ROOT/"
            # Modes as recorded, at the paths the files end up with.
            grep " $name/new/" "$FS/.modes" | sed "s# $name/new/# #; s#\\.Z\$##" >> "$MODES" || true
            [ -f "$sub/install/link" ] && cat "$sub/install/link" >> "$LINKS"
            subsets=$((subsets + 1))
        done
    fi
    printf '    %-28s %d file(s)\n' "$(basename "$f" .img)" "$n"
done

echo "==> expanding compress(1)ed files"
z=0
while IFS= read -r -d '' f; do
    [ "$(head -c 2 "$f" | od -An -tx1 | tr -d ' ')" = "1f9d" ] || continue
    if gzip -dc < "$f" > "$STAGE/z.tmp" 2>/dev/null; then
        cat "$STAGE/z.tmp" > "${f%.Z}"
        rm -f "$f"
        z=$((z + 1))
    fi
done < <(find "$ROOT" -type f -name '*.Z' -print0)
echo "    $z file(s)"

# The link scripts: `mv //dir/Long.N /dir/realname` restores a name the
# floppy could not hold, and `ln A B` makes a link.
echo "==> renames and links"
moved=0 linked=0
while read -r cmd a b _; do
    a=${a#/}; a=${a#/}; b=${b#/}; b=${b#/}
    case $a in dev/*) continue ;; esac
    [ -n "$a" ] && [ -n "$b" ] && [ -e "$ROOT/$a" ] || continue
    mkdir -p "$ROOT/$(dirname "$b")"
    if [ "$cmd" = mv ]; then
        mv -f "$ROOT/$a" "$ROOT/$b"
        sed -i "s# $a\$# $b#" "$MODES"
        moved=$((moved + 1))
    elif [ ! -e "$ROOT/$b" ] && [ ! -d "$ROOT/$a" ]; then
        ln "$ROOT/$a" "$ROOT/$b"
        linked=$((linked + 1))
    fi
done < <(grep -E '^[[:space:]]*(mv|ln)[[:space:]]+/' "$LINKS")
echo "    $moved rename(s), $linked link(s)"

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
echo "==> building $OUT (${SIZE_MB} MB, $files files, $subsets subset floppies)"
rm -f "$OUT"
fakeroot -- sh -c "
    chown -R 0:0 '$ROOT'
    mke2fs -q -F -b 1024 -t ext2 -L '$LABEL' -N $inodes \
        -d '$ROOT' '$OUT' ${SIZE_MB}M
"

# Modes are set in the image: many files are unreadable to their owner and
# were staged readable so they could be copied.
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
