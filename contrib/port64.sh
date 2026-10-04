#!/bin/sh
#
# port64.sh — build a contrib port for the 64-bit target with the
# x86_64-unknown-substrate cross toolchain.
#
#     contrib/port64.sh zlib            fetch + build + stage + sync
#     contrib/port64.sh --fetch zlib    fetch and patch only
#     contrib/port64.sh --build zlib    build an already-fetched tree
#
# Every port's fetch.sh and build.sh is written for the 32-bit target:
# they name i386-unknown-substrate tools, pass -march=i486, install into
# /usr/lib, build in <port>/build and stage into dist-overlay/dist-<port>.
# Rather than teach some 240 hand-written scripts a second target, this
# runs each port's OWN scripts through a fixed textual retargeting and
# executes the result:
#
#     i386-unknown-substrate      ->  x86_64-unknown-substrate
#     -march=i486, -mtune=i486    ->  dropped (the compiler's x86-64 default)
#     -m32, elf_i386*             ->  -m64, elf_x86_64*
#     <port>/build                ->  <port>/build64
#     dist-overlay/               ->  dist-overlay64/
#     --libdir=/usr/lib           ->  --libdir=/usr/lib64
#
# plus a CONFIG_SITE that makes /usr/lib64 the default libdir for autoconf
# ports that do not say.  The retargeted scripts are written beside the
# originals as .fetch64.sh / .build64.sh (ignored by git), because the
# scripts find their patches relative to their own location.
#
# So the 32-bit build of a port is untouched by its 64-bit build: separate
# source tree, separate objects, separate staging tree.  A port whose
# scripts need more than the textual retargeting carries the difference
# itself, behind a test of the triple or of $SUBSTRATE_ARCH, which this
# script exports as x86_64.
#
# After a successful build the port's libraries and headers are mirrored
# into the 64-bit toolchain's sysroot, so the next port's configure finds
# them (the 64-bit counterpart of scripts/sync-sysroot.sh's
# sync_to_sysroot).
#
# Env: STAGE1_PREFIX (default /opt/substrate), JOBS.

set -eu

MODE=all
case "${1:-}" in
    --fetch) MODE=fetch; shift ;;
    --build) MODE=build; shift ;;
esac
PKG="${1:-}"
[ -n "$PKG" ] || { echo "usage: port64.sh [--fetch|--build] <port>" >&2; exit 2; }

HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "$HERE/.." && pwd)"
PORT="$HERE/$PKG"
: "${STAGE1_PREFIX:=/opt/substrate}"
TRIPLE=x86_64-unknown-substrate
SYSROOT="$STAGE1_PREFIX/$TRIPLE"
STAGE="$SUBSTRATE_TOP/dist-overlay64/dist-$PKG"

[ -f "$PORT/build.sh" ] || { echo "port64.sh: no such port: $PKG" >&2; exit 1; }
[ -x "$STAGE1_PREFIX/bin/$TRIPLE-gcc" ] || {
    echo "port64.sh: no $TRIPLE-gcc under $STAGE1_PREFIX;" \
         "run contrib/build-toolchain64.sh first" >&2
    exit 1
}

export SUBSTRATE_TOP STAGE1_PREFIX
export SUBSTRATE_ARCH=x86_64
export PATH="$STAGE1_PREFIX/bin:$PATH"
export CONFIG_SITE="$HERE/config.site.x86_64"
# pkg-config must only ever answer from the 64-bit sysroot.
export PKG_CONFIG_LIBDIR="$SYSROOT/lib/pkgconfig:$SYSROOT/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=
unset PKG_CONFIG_PATH DESTDIR

retarget() {
    sed -e 's/i386-unknown-substrate/x86_64-unknown-substrate/g' \
        -e 's/i[3456]86-unknown-substrate/x86_64-unknown-substrate/g' \
        -e 's/ *-march=i[3456]86//g' \
        -e 's/ *-mtune=i[3456]86//g' \
        -e 's/\([^a-zA-Z0-9_]\)-m32\([^a-zA-Z0-9_]\)/\1-m64\2/g' \
        -e 's/elf_i386/elf_x86_64/g' \
        -e 's#\([{}/]\)build\([/"}]\)#\1build64\2#g' \
        -e 's#/build$#/build64#' \
        -e 's#dist-overlay/#dist-overlay64/#g' \
        -e 's#--libdir=/usr/lib\([^6a-zA-Z0-9_/]\|$\)#--libdir=/usr/lib64\1#g' \
        "$1" > "$2"
    chmod +x "$2"
}

if [ "$MODE" != build ]; then
    retarget "$PORT/fetch.sh" "$PORT/.fetch64.sh"
    # Reuse a tarball the 32-bit build already downloaded.
    mkdir -p "$PORT/build64"
    for f in "$PORT"/build/*; do
        [ -f "$f" ] || continue
        [ -e "$PORT/build64/$(basename "$f")" ] || cp "$f" "$PORT/build64/"
    done
    echo "==> [port64] $PKG: fetch"
    ( cd "$PORT" && ./.fetch64.sh )
fi

if [ "$MODE" != fetch ]; then
    retarget "$PORT/build.sh" "$PORT/.build64.sh"
    echo "==> [port64] $PKG: build"
    ( cd "$PORT" && ./.build64.sh )

    # A port that ignored libdir and installed 64-bit libraries into
    # /usr/lib would shadow nothing at link time but would never be found
    # by ld64.so at run time; move them where the 64-bit linker looks.
    if [ -d "$STAGE/usr/lib" ]; then
        mkdir -p "$STAGE/usr/lib64"
        for f in "$STAGE"/usr/lib/lib*.so* "$STAGE"/usr/lib/lib*.a; do
            [ -e "$f" ] || [ -L "$f" ] || continue
            mv "$f" "$STAGE/usr/lib64/"
        done
        if [ -d "$STAGE/usr/lib/pkgconfig" ]; then
            mkdir -p "$STAGE/usr/lib64/pkgconfig"
            for f in "$STAGE"/usr/lib/pkgconfig/*.pc; do
                [ -f "$f" ] || continue
                sed 's#^libdir=.*#libdir=/usr/lib64#' "$f" \
                    > "$STAGE/usr/lib64/pkgconfig/$(basename "$f")"
                rm -f "$f"
            done
            rmdir "$STAGE/usr/lib/pkgconfig" 2>/dev/null || true
        fi
        rmdir "$STAGE/usr/lib" 2>/dev/null || true
    fi

    # Mirror into the toolchain sysroot for the ports that follow.
    if [ -d "$STAGE/usr/lib64" ]; then
        mkdir -p "$SYSROOT/lib"
        cp -a "$STAGE/usr/lib64/." "$SYSROOT/lib/"
        # Libtool archives name /usr/lib64, which on the build host is the
        # HOST's library directory; without them -lfoo resolves through the
        # ordinary -L search, into the sysroot.
        ( cd "$STAGE/usr/lib64" && find . -name '*.la' ) 2>/dev/null |
        while IFS= read -r la; do
            rm -f "$SYSROOT/lib/$la"
        done
    fi
    if [ -d "$STAGE/usr/include" ]; then
        mkdir -p "$SYSROOT/include"
        cp -aL "$STAGE/usr/include/." "$SYSROOT/include/"
        for fix in "$STAGE1_PREFIX"/lib/gcc/$TRIPLE/*/include-fixed; do
            [ -d "$fix" ] || continue
            cp -aL "$STAGE/usr/include/." "$fix/" 2>/dev/null || true
        done
    fi
    echo "==> [port64] $PKG: staged at $STAGE"
fi
