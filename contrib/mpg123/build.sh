#!/bin/sh
#
# build.sh — build mpg123 for substrate.
#
# Substrate has a Sun-compat audio framework (NetBSD /dev/audio
# semantics) backed by Intel HDA, AC'97, SB16, and a null backend
# (see sys/drivers/audio/).  mpg123's "sun" output module targets
# exactly this ABI, so we use --with-default-audio=sun.
#
# Builds with the stage-1 cross-toolchain at /opt/substrate/ and
# installs the staged binary + libraries into the contrib local
# staging tree at ./build/install/.  build-rootfs.sh can pick those
# up via a future install rule.
#
# Substrate has no SSE / MMX (kernel-enforced), so we force the
# generic FPU code path: --with-cpu=generic_fpu.
#
# Env overrides:
#
#   SUBSTRATE_TOP        path to repo root (auto-detected from CLAUDE.md)
#   STAGE1_PREFIX        cross-toolchain prefix (default /opt/substrate)
#   TARGET_TRIPLE        default i386-unknown-substrate
#   PARALLEL             default $(nproc)
#   INSTALL_PREFIX       on-target install prefix (default /usr/local)
#
# Usage:
#   ./build.sh                       # configure + make + make install
#   ./build.sh --reconfigure         # re-run configure even if cached
#   ./build.sh --clean               # rm -rf build/build-substrate

set -eu

RECONFIGURE=0
DO_CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --reconfigure) RECONFIGURE=1 ;;
        --clean)       DO_CLEAN=1 ;;
        -h|--help)
            sed -n '/^# build\.sh/,/^# Usage:/p; /^# Usage:/,/^$/p' "$0"
            exit 0
            ;;
        *) echo "build.sh: unknown arg $arg" >&2; exit 2 ;;
    esac
done

HERE="$(cd "$(dirname "$0")" && pwd)"

# Locate substrate repo root by walking up.
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    SUBSTRATE_TOP="$HERE"
    while [ "$SUBSTRATE_TOP" != / ] \
        && [ ! -f "$SUBSTRATE_TOP/CLAUDE.md" ] \
        && [ ! -f "$SUBSTRATE_TOP/AGENTS.md" ]; do
        SUBSTRATE_TOP="$(dirname "$SUBSTRATE_TOP")"
    done
    if [ "$SUBSTRATE_TOP" = / ]; then
        echo "build.sh: cannot locate substrate repo root; set SUBSTRATE_TOP" >&2
        exit 1
    fi
fi

TARGET_TRIPLE="${TARGET_TRIPLE:-i386-unknown-substrate}"
STAGE1_PREFIX="${STAGE1_PREFIX:-/opt/substrate}"
PARALLEL="${PARALLEL:-$(nproc 2>/dev/null || echo 4)}"
# Install under /usr (not /usr/local) so build-rootfs.sh's contrib
# overlay loop picks the binary up at /usr/bin/mpg123.
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi

SRC_TREE="$(ls -d "$HERE"/build/mpg123-*/ 2>/dev/null | head -1 || true)"
if [ -z "$SRC_TREE" ]; then
    echo "build.sh: no source tree under $HERE/build/." >&2
    echo "         Run ./fetch.sh first." >&2
    exit 1
fi
SRC_TREE="${SRC_TREE%/}"

BUILD_DIR="$HERE/build/build-substrate"
# Stage into ${SUBSTRATE_TOP}/dist-overlay/dist-mpg123 so build-rootfs.sh's
# contrib-overlay loop picks it up the same way the other ports do.
INSTALL_DIR="${DESTDIR:-${SUBSTRATE_TOP}/dist-overlay/dist-mpg123}"

if [ "$DO_CLEAN" = 1 ]; then
    echo "==> Cleaning $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    exit 0
fi

# Cross-toolchain wrappers.  Verify they exist before configure tries
# to autoprobe with the host gcc and falls back to a non-cross build.
CROSS_CC="$STAGE1_PREFIX/bin/$TARGET_TRIPLE-gcc"
if [ ! -x "$CROSS_CC" ]; then
    echo "build.sh: cross compiler $CROSS_CC not found" >&2
    echo "         Run contrib/build-toolchain.sh --stage=1 first." >&2
    exit 1
fi

# The cross-gcc looks for libraries in both the binutils-style
# sysroot under $STAGE1_PREFIX/$TARGET_TRIPLE/lib/ AND under
# --with-sysroot=$SUBSTRATE_TOP/dist/{lib,usr/lib}/.  The first
# match wins, so if the binutils sysroot ships a stale libc.so.0
# the source-tree one in dist/ is shadowed.  It also looks for
# headers under <gcc>/include-fixed/, which is a frozen copy taken
# when stage 1 was installed.  Sync all of these from the source
# tree before configuring so probes resolve against fresh headers
# and the just-built libraries.
echo "==> Refreshing cross-toolchain sysroot + fixincludes from source tree"
SYSROOT_LIB="$STAGE1_PREFIX/$TARGET_TRIPLE/lib"
SYSROOT_INC="$STAGE1_PREFIX/$TARGET_TRIPLE/include"
# include-fixed/ lives next to the gcc-distributed limits.h etc.
GCC_INCLUDE_FIXED="$STAGE1_PREFIX/lib/gcc/$TARGET_TRIPLE/16.1.0/include-fixed"

if [ -d "$SYSROOT_LIB" ]; then
    for lib in c m dl pthread sys; do
        src="$SUBSTRATE_TOP/lib/$lib/lib$lib.so.0"
        if [ -f "$src" ]; then
            cp "$src" "$SYSROOT_LIB/lib$lib.so.0"
            ln -sfn "lib$lib.so.0" "$SYSROOT_LIB/lib$lib.so"
        fi
    done
fi

# Refresh every header from include/ into fixincludes so the cross
# compiler sees the same surface the kernel and dist build see.
# Also refresh dist/usr/include/ (the --with-sysroot location) so
# the userspace sysroot stays consistent.
sync_headers() {
    local dst="$1"
    [ -d "$dst" ] || return 0
    mkdir -p "$dst/sys" "$dst/arch/i386"
    cp -f "$SUBSTRATE_TOP"/include/*.h "$dst"/        2>/dev/null || true
    cp -f "$SUBSTRATE_TOP"/include/sys/*.h "$dst"/sys/ 2>/dev/null || true
    if [ -f "$SUBSTRATE_TOP/sys/arch/i386/syscall.h" ]; then
        cp -f "$SUBSTRATE_TOP/sys/arch/i386/syscall.h" "$dst/arch/i386/syscall.h"
    fi
    # The audio header lives kernel-side but is intended as a public ABI.
    if [ -f "$SUBSTRATE_TOP/sys/include/sys/audioio.h" ]; then
        cp -f "$SUBSTRATE_TOP/sys/include/sys/audioio.h" "$dst/sys/audioio.h"
    fi
}
sync_headers "$GCC_INCLUDE_FIXED"
sync_headers "$SUBSTRATE_TOP/dist/usr/include"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ ! -f Makefile ] || [ "$RECONFIGURE" = 1 ]; then
    echo "==> Configuring mpg123 for $TARGET_TRIPLE"
    # --with-cpu=generic_fpu       : substrate forbids SSE/MMX; use x87 only
    # --with-default-audio=sun     : Sun/NetBSD /dev/audio ABI matches our
    #                                in-kernel audio framework
    # --with-audio=sun             : build only the sun output module
    # --enable-modules (default)   : output modules loaded at runtime via
    #                                dlopen — substrate has libdl + ld.so
    #                                Phase 4e (dlopen/dlsym/dlclose)
    # --disable-network            : no in-kernel TCP/IP yet, http streaming
    #                                would just fail at runtime
    # --enable-shared (default)    : ship libmpg123.so + the binary
    # --enable-id3v2 / --enable-icy: keep the metadata parsers (pure C)
    # --disable-largefile          : 32-bit substrate, no 64-bit off_t yet
    # --disable-debug              : keep optimised release build
    # PKG_CONFIG=/bin/false        : suppress any host-pkg-config probes
    "$SRC_TREE/configure" \
        --host="$TARGET_TRIPLE" \
        --target="$TARGET_TRIPLE" \
        --prefix="$INSTALL_PREFIX" \
        --with-cpu=generic_fpu \
        --with-default-audio=sun \
        --with-audio=sun \
        --disable-network \
        --disable-largefile \
        --enable-id3v2 \
        --enable-icy \
        --disable-debug \
        PKG_CONFIG=/bin/false \
        CC="$CROSS_CC" \
        CFLAGS="-O2 -g -march=i486 -mtune=i486" \
        LDFLAGS=""
fi

echo "==> Building mpg123 (j$PARALLEL)"
make -j"$PARALLEL"

echo "==> Staging into $INSTALL_DIR"
rm -rf "$INSTALL_DIR"
make install DESTDIR="$INSTALL_DIR"

echo ""
echo "==> Done."
echo "    Binaries: $INSTALL_DIR$INSTALL_PREFIX/bin/{mpg123,mpg123-strip,mpg123-id3dump}"
echo "    Headers : $INSTALL_DIR$INSTALL_PREFIX/include/mpg123.h"
echo "    Library : $INSTALL_DIR$INSTALL_PREFIX/lib/libmpg123.a"
