#!/bin/sh
#
# build.sh — build GCC for substrate.
#
#   --stage=1    Cross-compiler.  build = host = Linux; target =
#                substrate.  Produces i386-unknown-substrate-gcc on
#                the Linux build host that emits substrate ELFs.
#                Requires binutils stage 1 already installed.
#
#   --stage=2    Native-on-substrate (Canadian cross).  build = Linux,
#                host = target = substrate, --prefix=/usr.  Compiles
#                gcc itself into substrate ELFs and stages into a
#                DESTDIR for dropping into rootfs.img.  Requires stage
#                1 of both gcc and binutils, plus the substrate libc
#                headers/libraries staged under SUBSTRATE_TOP/dist.
#
# Env knobs (with defaults):
#   STAGE1_PREFIX     /opt/substrate-toolchain
#   STAGE2_DESTDIR    /tmp/gcc-stage2-staging
#   PARALLEL          $(nproc)
#   TARGET_TRIPLE     i386-unknown-substrate
#   ENABLE_LANGUAGES  c                  (add "c,c++" once libstdc++
#                                         is sortable on substrate)
#

set -eu

STAGE=
for arg in "$@"; do
    case "$arg" in
        --stage=1) STAGE=1 ;;
        --stage=2) STAGE=2 ;;
        -h|--help)
            sed -n '/^# build\.sh/,/^# Usage:/p; /^# Usage:/,/^$/p' "$0"
            exit 0 ;;
        *) echo "build.sh: unknown arg $arg" >&2; exit 2 ;;
    esac
done
[ -n "$STAGE" ] || { echo "build.sh: must specify --stage=1 or --stage=2" >&2; exit 2; }

HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    SUBSTRATE_TOP="$HERE"
    while [ "$SUBSTRATE_TOP" != / ] && [ ! -f "$SUBSTRATE_TOP/AGENTS.md" ] \
        && [ ! -f "$SUBSTRATE_TOP/CLAUDE.md" ]; do
        SUBSTRATE_TOP="$(dirname "$SUBSTRATE_TOP")"
    done
    [ "$SUBSTRATE_TOP" != / ] || { echo "cannot locate substrate root" >&2; exit 1; }
fi

TARGET_TRIPLE="${TARGET_TRIPLE:-i386-unknown-substrate}"
PARALLEL="${PARALLEL:-$(nproc 2>/dev/null || echo 4)}"
STAGE1_PREFIX="${STAGE1_PREFIX:-/opt/substrate}"
# Stage-2 GCC stages into its OWN DESTDIR, separate from binutils.
# contrib/binutils/build.sh stages into ${SUBSTRATE_TOP}/dist-overlay/dist-toolchain
# and does `rm -rf` on it; if GCC used the same directory the second
# build to run would wipe the first (the symptom: `gcc` ends up on the
# image with no `as`/`ld`).  build-rootfs.sh --toolchain overlays BOTH
# dist-toolchain (binutils) and /tmp/gcc-stage2-staging (gcc).
STAGE2_DESTDIR="${STAGE2_DESTDIR:-/tmp/gcc-stage2-staging}"
ENABLE_LANGUAGES="${ENABLE_LANGUAGES:-c,c++}"

SRC_TREE="$(ls -d "$HERE"/build/gcc-*/ 2>/dev/null | head -1 || true)"
SRC_TREE="${SRC_TREE%/}"
if [ -z "$SRC_TREE" ] || [ ! -d "$SRC_TREE" ]; then
    echo "build.sh: no patched source tree under $HERE/build/ — run fetch.sh" >&2
    exit 1
fi

echo "==> SUBSTRATE_TOP      = $SUBSTRATE_TOP"
echo "==> source tree        = $SRC_TREE"
echo "==> stage              = $STAGE"
echo "==> ENABLE_LANGUAGES   = $ENABLE_LANGUAGES"

DISABLES="\
  --disable-multilib --disable-nls --disable-bootstrap \
  --disable-libgomp --disable-libitm \
  --disable-libsanitizer --disable-libquadmath --disable-libvtv \
  --disable-libssp --disable-libada --disable-libphobos \
  --disable-libcc1 --enable-shared"

if [ "$STAGE" = 1 ]; then
    # Stage-1 cross-toolchain.  Needs binutils stage 1 already
    # installed at $STAGE1_PREFIX/bin.
    if ! command -v "${TARGET_TRIPLE}-as" >/dev/null 2>&1; then
        echo "build.sh: ${TARGET_TRIPLE}-as not on PATH." >&2
        echo "          Build contrib/binutils stage 1 first." >&2
        exit 1
    fi

    BUILD_DIR="$HERE/build-stage1"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    echo "==> Configuring gcc stage 1 (cross from build host)"
    # --with-arch=i486 / --with-tune=i486 bake i486 as the default
    # -march/-mtune for everything this gcc compiles.  Substrate's
    # boot QEMU CPU (qemu32) has SSE/SSE2 but not the pentium-pro
    # default GCC would otherwise pick — and we don't want SSE in
    # the system compiler's output because it'd block running the
    # produced binaries on a plain i486 emulation.
    "$SRC_TREE/configure" \
        --target="$TARGET_TRIPLE" \
        --prefix="$STAGE1_PREFIX" \
        --with-sysroot="$SUBSTRATE_TOP/dist" \
        --with-arch=i486 \
        --with-tune=i486 \
        --enable-threads=posix \
        --enable-languages="$ENABLE_LANGUAGES" \
        $DISABLES

    # all-gcc gets us cc1 + the driver, enough to compile C.
    # all-target-libgcc gets the runtime; runs only after substrate
    # libc headers are visible via the sysroot.  Skip it for now if
    # the sysroot is sparse — try it, but don't fail the script.
    echo "==> Building cc1 + driver (-j $PARALLEL)"
    make -j "$PARALLEL" all-gcc

    # -std=gnu23 for the target compile.  libgcc is GCC's own code and
    # expects the modern C default, but patch 0011 makes substrate's cc
    # default to gnu17 (the K&R-era contrib ports need it), and there `bool`
    # is not a keyword:
    #
    #   gcc/config/i386/i386.h:1722: error: unknown type name 'bool'
    #
    # so libgcc did not build.  The script treats that as best-effort and
    # carries on, which turns a compiler with no runtime into a stage-1
    # "success" -- and every later link then fails with the far less
    # informative "C compiler cannot create executables".  The stage-2 block
    # below already passes this same flag for the same reason.
    LIBGCC_TARGET_CFLAGS="-g -O2 -std=gnu23"
    echo "==> Building libgcc (best-effort — needs substrate headers in sysroot)"
    make -j "$PARALLEL" CFLAGS_FOR_TARGET="$LIBGCC_TARGET_CFLAGS" all-target-libgcc || \
        echo "    libgcc build failed — likely missing headers/libs in sysroot." \
             "    Run again after staging more of dist/usr/include and dist/usr/lib."

    echo "==> Installing to $STAGE1_PREFIX"
    if [ -w "$(dirname "$STAGE1_PREFIX")" ]; then
        make install-gcc
        make CFLAGS_FOR_TARGET="$LIBGCC_TARGET_CFLAGS" install-target-libgcc || true
    else
        sudo make install-gcc
        sudo make install-target-libgcc || true
    fi

    cat <<EOF

==> Stage 1 complete.
    Compiler: $STAGE1_PREFIX/bin/${TARGET_TRIPLE}-gcc
    Add to PATH:  export PATH="$STAGE1_PREFIX/bin:\$PATH"

    Smoke test:
      echo 'int main(void){return 42;}' > /tmp/hi.c
      $STAGE1_PREFIX/bin/${TARGET_TRIPLE}-gcc -static -o /tmp/hi /tmp/hi.c
      file /tmp/hi
EOF
    exit 0
fi

if [ "$STAGE" = 2 ]; then
    if ! command -v "${TARGET_TRIPLE}-gcc" >/dev/null 2>&1; then
        echo "build.sh: stage 2 needs ${TARGET_TRIPLE}-gcc from stage 1." >&2
        exit 1
    fi
    BUILD_DIR="$HERE/build-stage2"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    BUILD_TRIPLE="$(cc -dumpmachine 2>/dev/null || echo x86_64-linux-gnu)"
    echo "==> Configuring gcc stage 2 (Canadian cross — runs on substrate)"
    # CC_FOR_BUILD / CXX_FOR_BUILD point at the BUILD machine's compiler
    # (Linux host gcc/g++) so build-time helpers (genmodes, gengtype,
    # ...) link against the host libc.  Without these, GCC's configure
    # defaults CC_FOR_BUILD = $(CC) = the cross compiler, and genmodes
    # gets built as a substrate-target ELF that can't run on the build
    # host.
    #
    # LDFLAGS="" suppresses the default `-static-libstdc++ -static-libgcc`
    # that gcc bakes into the bootstrap link line.  We *want* the host
    # binaries (cc1, cc1plus, lto1, lto-dump, xgcc, cpp) to DT_NEEDED
    # libstdc++.so.6 + libgcc_s.so.1 at runtime — substrate ships both
    # via build-libstdcxx-shared.sh + install-stripped-to-rootfs.sh.
    # Stage 2 is a Canadian cross — host=target=substrate, so configure
    # tests can't run the binaries they produce.  Preset autoconf cache
    # variables for things the substrate toolchain knows up-front: i486
    # is little-endian, has 8-bit chars, etc.  Without ac_cv_c_bigendian
    # the mpc subbuild dies with "configure: error: unknown endianness".
    export ac_cv_c_bigendian=no
    # Every C++ program linked with the substrate toolchain needs -lpthread.
    # libstdc++.a's exception-allocation pool (eh_alloc.o) calls
    # __gthread_mutex_lock/unlock, and our libstdc++ carries no libpthread
    # dependency of its own, so the references arrive unresolved at the final
    # link:
    #
    #   libstdc++.a(eh_alloc.o): in function `__gthread_mutex_lock(int*)':
    #   gthr-default.h:795: undefined reference to `pthread_mutex_lock'
    #
    # That bites every C++ binary this build produces -- isl_test_cpp is
    # simply the first one in dependency order, and cc1plus would be next.
    # It goes in LIBS rather than LDFLAGS because autoconf puts LDFLAGS
    # *before* the objects on the link line, where a static archive
    # contributes nothing; LIBS is placed last, after the objects that
    # reference it.  Exported so every subdirectory configure and make
    # inherits it, and repeated on the configure and make lines because
    # GCC's Makefiles re-derive it per subdirectory.
    export LIBS="-lpthread"
    # The target libraries (libgcc first) are GCC's own runtime, and GCC 16
    # writes its sources in C23 -- gcc/config/i386/i386.h:1722 declares a bare
    # `bool preserve_none_abi;`.  patches/0011-c-default-gnu17.patch makes the
    # C front end default to gnu17 so that old K&R contrib ports still build,
    # and under gnu17 `bool` is not a keyword:
    #
    #   gcc/config/i386/i386.h:1722:3: error: unknown type name 'bool'
    #
    # libgcc is compiled by the stage-1 cross compiler (a Canadian cross cannot
    # run the stage-2 one on the build host), so it inherits that default and
    # dies.  The gnu17 default is a policy for *user* code; GCC's own runtime
    # wants the dialect GCC was written in.  Ask for it explicitly here rather
    # than narrowing patch 0011, which cannot tell whose source it is reading.
    CFLAGS_FOR_TARGET="-g -O2 -std=gnu23"

    # libstdc++ must not see the *stage-1* C++ headers already installed in
    # the sysroot.  A native GCC build compiles the target libraries with an
    # in-tree xg++ that carries -nostdinc++; a Canadian cross has no runnable
    # in-tree compiler, so CXX_FOR_TARGET is the installed stage-1 driver and
    # that flag is absent.  Two copies of libstdc++'s compat headers then sit
    # on one search path, and #include_next dead-ends on the shared guard:
    #
    #   include-fixed/math.h  ->  #include <fenv.h>
    #     -> build-stage2/.../libstdc++-v3/include/fenv.h   sets _GLIBCXX_FENV_H,
    #                                                       then #include_next
    #       -> <sysroot>/include/c++/16.1.0/fenv.h          guard already set,
    #                                                       expands to nothing
    #
    # so Substrate's real <fenv.h> is never reached and every declaration in
    # math.h that names fenv_t (fromfp, ufromfpx, ...) fails to compile:
    #
    #   include-fixed/math.h:622:33: error: 'fenv_t' has not been declared
    #
    # It has to be set here, not on the make line: config.status bakes CXX
    # into each target library's Makefile, where a make-level CXX_FOR_TARGET
    # override no longer reaches it.
    CXX_FOR_TARGET="${TARGET_TRIPLE}-c++ -nostdinc++"
    "$SRC_TREE/configure" \
        --build="$BUILD_TRIPLE" \
        --host="$TARGET_TRIPLE" \
        --target="$TARGET_TRIPLE" \
        --prefix=/usr \
        --with-sysroot=/ \
        --with-arch=i486 \
        --with-tune=i486 \
        --enable-threads=posix \
        --enable-languages="$ENABLE_LANGUAGES" \
        $DISABLES \
        CC="${TARGET_TRIPLE}-gcc" \
        CXX="${TARGET_TRIPLE}-g++" \
        AR="${TARGET_TRIPLE}-ar" \
        AS="${TARGET_TRIPLE}-as" \
        LD="${TARGET_TRIPLE}-ld" \
        NM="${TARGET_TRIPLE}-nm" \
        RANLIB="${TARGET_TRIPLE}-ranlib" \
        STRIP="${TARGET_TRIPLE}-strip" \
        CC_FOR_BUILD=gcc \
        CXX_FOR_BUILD=g++ \
        ac_cv_c_bigendian=no \
        LDFLAGS="" \
        LIBS="-lpthread" \
        CFLAGS_FOR_TARGET="$CFLAGS_FOR_TARGET" \
        CXX_FOR_TARGET="$CXX_FOR_TARGET"

    echo "==> Building (-j $PARALLEL)"
    # Pass through again on the make line — GCC's Makefiles re-derive
    # CC_FOR_BUILD/CXX_FOR_BUILD/LDFLAGS for subdirectories and the
    # cleanest way to keep them honest is to repeat the override.
    make -j "$PARALLEL" \
        CC_FOR_BUILD=gcc \
        CXX_FOR_BUILD=g++ \
        CFLAGS_FOR_TARGET="$CFLAGS_FOR_TARGET"

    echo "==> Staging to $STAGE2_DESTDIR"
    rm -rf "$STAGE2_DESTDIR"
    mkdir -p "$STAGE2_DESTDIR"
    make install DESTDIR="$STAGE2_DESTDIR" \
        CC_FOR_BUILD=gcc \
        CXX_FOR_BUILD=g++ \
        CFLAGS_FOR_TARGET="$CFLAGS_FOR_TARGET"

    # lto-dump isn't part of the default install target — copy it
    # explicitly so it lands in $STAGE2_DESTDIR/usr/libexec/...
    LTO_DUMP_SRC="$BUILD_DIR/gcc/lto-dump"
    LTO_DUMP_DST="$STAGE2_DESTDIR/usr/libexec/gcc/${TARGET_TRIPLE}/$(cat "$SRC_TREE/gcc/BASE-VER")/lto-dump"
    if [ -x "$LTO_DUMP_SRC" ]; then
        install -m 755 "$LTO_DUMP_SRC" "$LTO_DUMP_DST"
    fi

    # Strip after lto-dump lands, so it gets stripped too.  GCC's install
    # targets never strip and everything here was built -g, which is how
    # cc1/cc1plus/lto1/lto-dump reached ~370 MB apiece.
    "$HERE/../strip-staging.sh" "$STAGE2_DESTDIR" "gcc stage-2"

    cat <<EOF

==> Stage 2 complete.
    Substrate-ELF binaries: $STAGE2_DESTDIR/usr/bin/
EOF
    exit 0
fi
