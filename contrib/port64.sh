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
#     /usr/lib (the directory)    ->  /usr/lib64    (not /usr/libexec &c.)
#     lib/X/libX.so.0, .a, crt*.o ->  lib/X/obj-x86_64/...   (the in-tree
#                                     libraries' 64-bit build)
#     substrate-autotools.sh      ->  its own retargeted copy
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
# pkg-config, for the cross build, must only ever answer from the 64-bit
# sysroot, and with the sysroot in front of every path it hands out.  This
# matters more here than for the 32-bit target.  A .pc file says
# libdir=/usr/lib64, the on-target path; unprefixed, that is -L/usr/lib64
# -- the BUILD HOST's library directory.  A 32-bit link skips what it finds
# there as the wrong format; a 64-bit one is link-compatible with it and
# takes the host's libz.so without a word.  The sysroot keeps its libraries
# in lib/ and its headers in include/, so give it the usr/ names the .pc
# files use.
#
# It is given to the cross build alone, as x86_64-unknown-substrate-
# pkg-config: a configure run with --host looks for that name before
# plain pkg-config.  Exporting PKG_CONFIG_LIBDIR instead would also reach
# the build-HOST stages some ports have (python builds a host interpreter
# first, and its configure then found substrate's tcl), which must see the
# host's own .pc files.  A script that sets PKG_CONFIG_LIBDIR or
# PKG_CONFIG_SYSROOT_DIR itself is taken at its word.
#
# For a build that calls plain pkg-config, as the 32-bit one does, the
# host's pkg-config already drops -L/usr/lib as a system directory; tell it
# /usr/lib64 is one too.
mkdir -p "$SYSROOT/usr" "$SYSROOT/lib" "$SYSROOT/include"
[ -e "$SYSROOT/usr/lib64" ]   || ln -s ../lib "$SYSROOT/usr/lib64"
[ -e "$SYSROOT/usr/lib" ]     || ln -s ../lib "$SYSROOT/usr/lib"
[ -e "$SYSROOT/usr/include" ] || ln -s ../include "$SYSROOT/usr/include"
mkdir -p "$HERE/.bin64"
cat > "$HERE/.bin64/$TRIPLE-pkg-config" <<EOF
#!/bin/sh
# Written by contrib/port64.sh: pkg-config for the 64-bit cross build.
: "\${PKG_CONFIG_LIBDIR:=$SYSROOT/lib/pkgconfig:$SYSROOT/share/pkgconfig}"
: "\${PKG_CONFIG_SYSROOT_DIR:=$SYSROOT}"
export PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
exec pkg-config "\$@"
EOF
chmod +x "$HERE/.bin64/$TRIPLE-pkg-config"
export PATH="$HERE/.bin64:$PATH"
export PKG_CONFIG_SYSTEM_LIBRARY_PATH=/usr/lib64:/usr/lib
unset PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR DESTDIR

retarget() {
    sed -e 's/i386-unknown-substrate/x86_64-unknown-substrate/g' \
        -e 's/i[3456]86-unknown-substrate/x86_64-unknown-substrate/g' \
        -e 's/ *-march=i[3456]86//g' \
        -e 's/ *-mtune=i[3456]86//g' \
        -e 's/\([^a-zA-Z0-9_]\)-m32\([^a-zA-Z0-9_]\)/\1-m64\2/g' \
        -e 's/elf_i386/elf_x86_64/g' \
        -e 's/\([^a-zA-Z0-9_]\)ABI=32\([^0-9]\|$\)/\1ABI=64\2/g' \
        -e 's#\([{}/]\)build\([/"}]\)#\1build64\2#g' \
        -e 's#/build$#/build64#' \
        -e 's#dist-overlay/#dist-overlay64/#g' \
        -e 's#/usr/lib\([^6a-zA-Z0-9_.-]\|$\)#/usr/lib64\1#g' \
        -e 's#/\(usr\.\)\{0,1\}lib/\([a-z]*\|\$[{]\{0,1\}[_a-zA-Z]*[}]\{0,1\}\)/\(lib[^/ "]*\.\(so\.0\|a\)\)#/\1lib/\2/obj-x86_64/\3#g' \
        -e 's#/lib/c/\(crt[0in]\.o\)#/lib/c/obj-x86_64/\1#g' \
        -e 's#/sbin/ld\.so/ld\.so#/sbin/ld.so/obj-x86_64/ld64.so#g' \
        -e 's#/binutils/build64/#/binutils/build/#g' \
        -e 's#\(substrate-[a-z-]*\)\.sh#.\164.sh#g' \
        -e 's#\(dynamic-linker[=, ]\)/sbin/ld\.so#\1/sbin/ld64.so#g' \
        -e 's#\(toolchain\)\.cmake#\1.x86_64.cmake#g' \
        -e 's#SYSTEM_PROCESSOR i[3456]86#SYSTEM_PROCESSOR x86_64#g' \
        "$1" > "$2"
    chmod +x "$2"
}

# The helpers the build.sh files source (substrate-autotools.sh, which
# about a hundred of them use to assemble sysroots out of the staging trees
# and the in-tree libraries; substrate-codec.sh, which IS the fetch and
# build of the audio codecs) are retargeted the same way, and the scripts
# are pointed at the copies.
#
# One path is put back: the helpers take config.sub from the binutils
# port's extracted tree, contrib/binutils/build, which is not a port tree
# to be redirected -- and whose config.sub is the only one that knows
# x86_64-unknown-substrate when the port's own dates from 2001.
for helper in "$HERE"/substrate-*.sh; do
    [ -f "$helper" ] || continue
    retarget "$helper" "$HERE/.$(basename "${helper%.sh}")64.sh"
done

# Likewise the CMake toolchain files, which name the compiler, the sysroot
# and the processor where no build.sh rule can reach them: a port built
# through an unretargeted one comes out 32-bit without complaint.  Each
# <name>toolchain.cmake gets a retargeted <name>toolchain.x86_64.cmake
# beside it, where its relative references still resolve.
for tc in "$HERE"/*/*toolchain.cmake; do
    [ -f "$tc" ] || continue
    retarget "$tc" "${tc%.cmake}.x86_64.cmake"
    chmod -x "${tc%.cmake}.x86_64.cmake"
done

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

    # Libtool archives name libdir='/usr/lib64', which on the build host is
    # the HOST's library directory: given -lfoo, libtool finds libfoo.la
    # and links /usr/lib64/libfoo.so -- the host's, successfully.  Nothing
    # on the image needs them, so they are dropped from the staging tree
    # itself, not only from the sysroot copy.
    find "$STAGE" -name '*.la' -exec rm -f {} +

    # Nothing staged may have been linked against the build host.  A glibc
    # symbol version or soname in a substrate binary means some -L reached
    # a host directory; fail here, where the port is known, rather than on
    # the target, where it is a library that will not load.
    hostlinked=$(find "$STAGE" -type f | while IFS= read -r f; do
        if "$TRIPLE-readelf" -dV "$f" 2>/dev/null |
               grep -q 'GLIBC_\|libc\.so\.6\|ld-linux'; then
            echo "$f"
        fi
    done)
    if [ -n "$hostlinked" ]; then
        echo "port64.sh: $PKG linked against the BUILD HOST's libraries:" >&2
        echo "$hostlinked" | sed 's/^/    /' >&2
        exit 1
    fi

    # Nor may anything staged be a 32-bit object: that is a part of the
    # port's build the retargeting did not reach (a toolchain file, a
    # hard-coded compiler name), and on the image it is a program that
    # needs 32-bit libraries the port never provided.  The same goes for a
    # 64-bit program that names the 32-bit dynamic linker, which a port
    # that spells out its own link line can produce: the kernel refuses it
    # with ENOEXEC.
    wrongclass=$(find "$STAGE" -type f | while IFS= read -r f; do
        if [ "$(od -An -tu1 -N5 "$f" 2>/dev/null | tr -s ' ')" = " 127 69 76 70 1" ]; then
            echo "$f"
        elif "$TRIPLE-readelf" -l "$f" 2>/dev/null |
                 grep -q 'program interpreter: /sbin/ld\.so\]'; then
            echo "$f (interpreter /sbin/ld.so)"
        fi
    done)
    if [ -n "$wrongclass" ]; then
        echo "port64.sh: $PKG staged 32-bit objects:" >&2
        echo "$wrongclass" | sed 's/^/    /' >&2
        exit 1
    fi

    # Mirror into the toolchain sysroot for the ports that follow.
    if [ -d "$STAGE/usr/lib64" ]; then
        mkdir -p "$SYSROOT/lib"
        # --remove-destination: e2fsprogs installs its archives read-only,
        # and a rebuild must be able to replace the copy from last time.
        cp -a --remove-destination "$STAGE/usr/lib64/." "$SYSROOT/lib/"
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
