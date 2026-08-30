#!/bin/sh
#
# build-image.sh - build the /perso/xenix286s disk image from SCO Xenix media.
#
# Produces an ext2 image holding a populated Xenix/286 userland.  The image is
# ext2 rather than a Xenix filesystem on purpose: substrate reads it through
# its own VFS and runs the x.out binaries under PERS_SCO_X286, so the
# container format is substrate's business and only the file contents are
# Xenix's.  (To read a genuine Xenix filesystem, see xenixfs.py beside this.)
#
# WHERE THE FILES COME FROM
#   rts/{b1,b2,x1..x4,ga,n2,n3}.img   tar archives: the runtime system and
#                                     extended utilities (~1047 files)
#   rts/n1.img                        NOT a tar -- a bootable Xenix filesystem,
#                                     and the ONLY source of /bin/sh.  The tar
#                                     volumes ship no sh, cp, ln, mv, tar,
#                                     cpio, dd, ps or mkdir at all; every one
#                                     of them lives on the install floppy.
#                                     Read out with xenixfs.py.
#   msw/word.img                      Microsoft Word 3.0
#
# The shipped termdesc has a damaged "ansi" entry -- verified identical on the
# 1987 distribution floppy, so it is Microsoft's defect, not media rot -- and
# bin/xenix/fix-termdesc-ansi.sh repairs it here.  See that script for why.
#
# EXECUTE-ONLY FILES
#   Xenix ships ~20 uucp binaries mode 0111/0100, which the build user cannot
#   read.  They are staged u+r so mke2fs can copy them, then their real modes
#   are restored inside the image with debugfs.
#
# Usage: build-image.sh [-m MEDIA_DIR] [-o OUTPUT] [-s SIZE_MB] [--minimal]
#
# Installs every product on the media by default; --minimal is the runtime
# system plus Word only.

set -e

MEDIA=${MEDIA:-$HOME/Downloads/286}
OUT=""
SIZE_MB=64
MINIMAL=no
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$HERE/../.." && pwd)

while [ $# -gt 0 ]; do
    case "$1" in
    -m|--media)  MEDIA=$2; shift 2 ;;
    -o|--output) OUT=$2; shift 2 ;;
    -s|--size)   SIZE_MB=$2; shift 2 ;;
    --minimal)   MINIMAL=yes; shift ;;
    -h|--help)   sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "$0: unknown argument: $1" >&2; exit 64 ;;
    esac
done
[ -n "$OUT" ] || OUT=$TOP/xenix286s.img

for tool in mke2fs fakeroot debugfs e2fsck tar python3; do
    command -v "$tool" >/dev/null || { echo "$0: need $tool" >&2; exit 65; }
done
[ -d "$MEDIA/rts" ] || { echo "$0: no rts/ under $MEDIA" >&2; exit 66; }

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT INT TERM
ROOT=$STAGE/root
mkdir -p "$ROOT"

echo "==> staging the runtime system from tar volumes"
# ./tmp holds the `custom` installer's scaffolding (_lbl volume labels, perms
# manifests, init.* product hooks, and payloads it would place itself); none
# of it belongs in a runtime /tmp.
for vol in b1 b2 x1 x2 x3 x4 ga n2 n3; do
    [ -f "$MEDIA/rts/$vol.img" ] || continue
    ( cd "$ROOT" && tar xf "$MEDIA/rts/$vol.img" \
        --exclude='./tmp' --exclude='./tmp/*' ) 2>/dev/null || true
done

echo "==> extracting /bin from the install floppy (the only source of sh)"
"$HERE/xenixfs.py" extract "$MEDIA/rts/n1.img" "$STAGE/n1bin" --path /bin
cp -a "$STAGE/n1bin"/. "$ROOT/bin/"

if [ -f "$MEDIA/msw/word.img" ]; then
    echo "==> adding Microsoft Word 3.0"
    ( cd "$ROOT" && tar xf "$MEDIA/msw/word.img" )
    if [ -f "$ROOT/usr/lib/MSTOOLS/termdesc" ]; then
        "$TOP/bin/xenix/fix-termdesc-ansi.sh" "$ROOT/usr/lib/MSTOOLS/termdesc"
    fi
fi

if [ "$MINIMAL" = no ]; then
    # ---- the rest of the library ------------------------------------
    #
    # Three packaging shapes, and only the first can be untarred blindly:
    #
    #  1. `custom` format -- real files under ./bin ./etc ./lib ./usr plus
    #     ./tmp holding the installer's scaffolding (_lbl, perms, fixperm,
    #     brand, install, init.*).  Take everything but ./tmp.
    #  2. msinstall format (BASIC) -- members named ../../usr/... , meant to
    #     be untarred from a directory two levels down.  tar refuses '..'
    #     without -P; --strip-components=2 then lands them at usr/... .
    #  3. no paths at all (Multiplan) -- bare MP.HLP, mp, mp.exec, termcap.
    #     The `mp` wrapper execs /usr/lib/MSTOOLS/mp.exec, which is where
    #     the payload belongs; mp itself is /usr/bin/mp.
    install_custom() {
        for img in "$@"; do
            [ -f "$img" ] || continue
            ( cd "$ROOT" && tar xf "$img" --exclude='./tmp' --exclude='./tmp/*' ) \
                2>/dev/null || true
        done
    }

    echo "==> Development System"
    install_custom "$MEDIA"/dev/dev[1-4].img
    echo "==> manual pages"
    install_custom "$MEDIA"/man/m[1-6].img
    echo "==> text processing"
    install_custom "$MEDIA"/txt/txt[1-4].img
    echo "==> CGI graphics"
    install_custom "$MEDIA"/cgi/cgi[1-4].img
    echo "==> Lyrix"
    install_custom "$MEDIA"/lrx/lyrix[1-4].img
    echo "==> VS COBOL"
    install_custom "$MEDIA"/vsc/cobol[1-2].img
    echo "==> FoxBASE"
    install_custom "$MEDIA"/fox/fox1.img "$MEDIA"/fox/foxplus[1-3].img
    echo "==> misc (PDS, PET, chat, gnome, tetris)"
    install_custom "$MEDIA"/msc/pds[1-2].img "$MEDIA"/msc/pet.img \
                   "$MEDIA"/msc/chat286.img "$MEDIA"/msc/gnome286.img \
                   "$MEDIA"/msc/tetris286.img

    if [ -f "$MEDIA/msc/utils286.tar.Z" ]; then
        echo "==> precompiled utilities (emacs, kermit, ...)"
        ( cd "$ROOT" && zcat "$MEDIA/msc/utils286.tar.Z" | tar xf - ) || true
    fi

    if [ -f "$MEDIA/msb/basic1.img" ]; then
        echo "==> Microsoft BASIC"
        B=$STAGE/basic; mkdir -p "$B"
        for v in 1 2 3; do
            [ -f "$MEDIA/msb/basic$v.img" ] || continue
            ( cd "$B" && tar xPf "$MEDIA/msb/basic$v.img" \
                --strip-components=2 --wildcards '../../*' ) 2>/dev/null || true
        done
        [ -d "$B/usr" ] && cp -a "$B/usr/." "$ROOT/usr/"
    fi

    if [ -f "$MEDIA/msp/d1.img" ]; then
        echo "==> Multiplan 2.0"
        M=$STAGE/mp; mkdir -p "$M"
        for v in 1 2; do
            [ -f "$MEDIA/msp/d$v.img" ] || continue
            ( cd "$M" && tar xf "$MEDIA/msp/d$v.img" ) 2>/dev/null || true
        done
        mkdir -p "$ROOT/usr/lib/MSTOOLS" "$ROOT/usr/bin"
        for f in mp.exec MP.HLP termcap amor.mod deprec.mod price.mod \
                 startup.mod; do
            [ -f "$M/$f" ] && cp -a "$M/$f" "$ROOT/usr/lib/MSTOOLS/$f"
        done
        if [ -f "$M/mp" ]; then
            cp -a "$M/mp" "$ROOT/usr/bin/mp"; chmod 755 "$ROOT/usr/bin/mp"
        fi
    fi

    if [ -f "$MEDIA/msc/deco286.tar.Z" ]; then
        echo "==> Demos Commander"
        D=$STAGE/deco; mkdir -p "$D"
        ( cd "$D" && zcat "$MEDIA/msc/deco286.tar.Z" | tar xf - ) || true
        if [ -d "$D/dist286" ]; then
            mkdir -p "$ROOT/usr/lib/deco/help" "$ROOT/usr/bin"
            cp -a "$D/dist286/deco" "$ROOT/usr/bin/deco" 2>/dev/null || true
            chmod 755 "$ROOT/usr/bin/deco" 2>/dev/null || true
            [ -d "$D/dist286/lib" ]  && cp -a "$D/dist286/lib/."  "$ROOT/usr/lib/deco/"
            [ -d "$D/dist286/help" ] && cp -a "$D/dist286/help/." "$ROOT/usr/lib/deco/help/"
            for doc in README RUSREADME ref.man cyrref.man; do
                [ -f "$D/dist286/$doc" ] && cp -a "$D/dist286/$doc" "$ROOT/usr/lib/deco/"
            done
        fi
    fi
fi

# MSTOOLS writes scratch files here and does not create it itself.
mkdir -p "$ROOT/usr/tmp" "$ROOT/tmp"
chmod 1777 "$ROOT/tmp" "$ROOT/usr/tmp"

echo "==> staging execute-only files readable so they can be copied"
MODES=$STAGE/modes.txt
: > "$MODES"
find "$ROOT" -type f ! -perm -u+r -printf '%m %P\n' > "$MODES"
while read -r mode path; do
    chmod u+r "$ROOT/$path"
done < "$MODES"
echo "    $(wc -l < "$MODES") file(s) staged readable"

echo "==> building $OUT (${SIZE_MB}M, ext2, 1024-byte blocks)"
rm -f "$OUT"
fakeroot -- sh -c "
    chown -R 0:0 '$ROOT' &&
    mke2fs -q -F -b 1024 -t ext2 -L xenix286 -N 8192 \
        -O ext_attr,resize_inode,dir_index,filetype,sparse_super,large_file \
        -d '$ROOT' '$OUT' $((SIZE_MB * 1024))
"

if [ -s "$MODES" ]; then
    echo "==> restoring execute-only modes inside the image"
    CMDS=$STAGE/debugfs.cmd
    : > "$CMDS"
    while read -r mode path; do
        # debugfs wants the full mode word: S_IFREG | permissions.
        printf 'sif /%s mode 0100%s\n' "$path" "$mode" >> "$CMDS"
    done < "$MODES"
    debugfs -w -f "$CMDS" "$OUT" >/dev/null 2>&1
fi

echo "==> verifying"
e2fsck -fp "$OUT" >/dev/null 2>&1 || {
    rc=$?
    [ "$rc" -le 1 ] || { echo "$0: e2fsck reported $rc" >&2; exit 1; }
}
files=$(find "$ROOT" -type f | wc -l)
echo "    $files files, $(du -h "$OUT" | cut -f1) image, fsck clean"
echo "    shell: $(debugfs -R 'stat /bin/sh' "$OUT" 2>/dev/null | grep -o 'Size: [0-9]*' | head -1)"
echo "done: $OUT"
