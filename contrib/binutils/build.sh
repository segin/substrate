#!/bin/sh
#
# build.sh — build binutils for substrate.
#
# Two stages:
#
#   --stage=1    Cross-toolchain.  Build host = Linux, target = substrate.
#                Produces i386-unknown-substrate-{as,ld,ar,nm,...} that
#                run on the build machine and emit substrate ELFs.
#                Installs into $STAGE1_PREFIX (default /opt/substrate-
#                toolchain).  This is what you need to compile substrate
#                binaries from a Linux box.
#
#   --stage=2    Native-on-substrate.  Build host = Linux, runtime host
#                = substrate.  Uses the stage-1 cross-toolchain (and a
#                substrate-target gcc — see contrib/gcc/) to produce
#                binutils binaries that are themselves substrate ELFs.
#                Installs --prefix=/usr into a staging DESTDIR so the
#                image build can drop /usr/bin/{as,ld,...} into the
#                substrate rootfs.  Requires contrib/gcc/ to have been
#                built through stage 1.
#
# Env overrides:
#
#   SUBSTRATE_TOP        path to the substrate repo root (default: walk up
#                        until we find AGENTS.md / CLAUDE.md)
#   STAGE1_PREFIX        install prefix for stage 1 (default
#                        /opt/substrate-toolchain)
#   STAGE2_DESTDIR       DESTDIR for stage-2 staging (default
#                        ${SUBSTRATE_TOP}/dist-overlay/dist-toolchain)
#   TARGET_TRIPLE        default i386-unknown-substrate
#   PARALLEL             default $(nproc)
#
# Usage:
#   ./build.sh --stage=1                      # cross-toolchain
#   ./build.sh --stage=2                      # native (needs stage 1 of gcc)
#   STAGE1_PREFIX=/tmp/foo ./build.sh --stage=1

set -eu

# ------------------------------------------------------------------ args
STAGE=
for arg in "$@"; do
    case "$arg" in
        --stage=1) STAGE=1 ;;
        --stage=2) STAGE=2 ;;
        -h|--help)
            sed -n '/^# build\.sh/,/^# Usage:/p; /^# Usage:/,/^$/p' "$0"
            exit 0
            ;;
        *) echo "build.sh: unknown arg $arg" >&2; exit 2 ;;
    esac
done
if [ -z "$STAGE" ]; then
    echo "build.sh: must specify --stage=1 or --stage=2" >&2
    exit 2
fi

HERE="$(cd "$(dirname "$0")" && pwd)"

# Find substrate repo root by walking up from $HERE.
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    SUBSTRATE_TOP="$HERE"
    while [ "$SUBSTRATE_TOP" != / ] && [ ! -f "$SUBSTRATE_TOP/AGENTS.md" ] \
        && [ ! -f "$SUBSTRATE_TOP/CLAUDE.md" ]; do
        SUBSTRATE_TOP="$(dirname "$SUBSTRATE_TOP")"
    done
    if [ "$SUBSTRATE_TOP" = / ]; then
        echo "build.sh: cannot locate substrate repo root; set SUBSTRATE_TOP" >&2
        exit 1
    fi
fi

TARGET_TRIPLE="${TARGET_TRIPLE:-i386-unknown-substrate}"
PARALLEL="${PARALLEL:-$(nproc 2>/dev/null || echo 4)}"
STAGE1_PREFIX="${STAGE1_PREFIX:-/opt/substrate}"
STAGE2_DESTDIR="${STAGE2_DESTDIR:-${SUBSTRATE_TOP}/dist-overlay/dist-toolchain}"

# Locate the patched source tree.  fetch.sh extracts to ./build/binutils-X.Y.Z/
SRC_TREE="$(ls -d "$HERE"/build/binutils-*/ 2>/dev/null | head -1 || true)"
if [ -z "$SRC_TREE" ] || [ ! -d "$SRC_TREE" ]; then
    echo "build.sh: no patched source tree under $HERE/build/" >&2
    echo "          run ./fetch.sh first" >&2
    exit 1
fi
SRC_TREE="${SRC_TREE%/}"

echo "==> SUBSTRATE_TOP     = $SUBSTRATE_TOP"
echo "==> source tree       = $SRC_TREE"
echo "==> TARGET_TRIPLE     = $TARGET_TRIPLE"
echo "==> stage             = $STAGE"

# ------------------------------------------------------------------ stage 1
if [ "$STAGE" = 1 ]; then
    BUILD_DIR="$HERE/build-stage1"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    echo "==> Configuring stage 1 (cross from build host)"
    "$SRC_TREE/configure" \
        --target="$TARGET_TRIPLE" \
        --prefix="$STAGE1_PREFIX" \
        --with-sysroot="$SUBSTRATE_TOP/dist" \
        --disable-werror --disable-nls \
        --disable-gdb --disable-gdbserver --disable-sim \
        --disable-multilib

    echo "==> Building (-j $PARALLEL)"
    make -j "$PARALLEL"

    echo "==> Installing to $STAGE1_PREFIX"
    if [ -w "$(dirname "$STAGE1_PREFIX")" ]; then
        make install
    else
        echo "    (sudo required for $STAGE1_PREFIX)"
        sudo make install
    fi

    echo ""
    echo "==> Stage 1 complete."
    echo "    Toolchain at: $STAGE1_PREFIX/bin/$TARGET_TRIPLE-{as,ld,ar,...}"
    echo "    Add to PATH:  export PATH=\"$STAGE1_PREFIX/bin:\$PATH\""
    exit 0
fi

# ------------------------------------------------------------------ stage 2
if [ "$STAGE" = 2 ]; then
    # Sanity: the stage-1 substrate-target gcc must be on PATH.
    if ! command -v "${TARGET_TRIPLE}-gcc" >/dev/null 2>&1; then
        cat >&2 <<EOF
build.sh: stage 2 needs ${TARGET_TRIPLE}-gcc on PATH.

  Stage 2 produces binutils binaries that are themselves substrate ELFs
  (so they can be dropped into the rootfs image and run on substrate).
  That requires a substrate-target C compiler — gcc patched for the
  substrate target, vendored at contrib/gcc/ (not yet present in this
  tree).

  Stage 1 cross-toolchain works today.  Stage 2 is parked until
  contrib/gcc/ is up.
EOF
        exit 1
    fi

    BUILD_DIR="$HERE/build-stage2"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    echo "==> Configuring stage 2 (Canadian cross — runs on substrate)"
    # Canadian cross: build (current machine) != host (substrate) ==
    # target (substrate).  --prefix=/usr because the binaries we
    # produce live at /usr/bin/{as,ld,...} on the substrate rootfs.
    BUILD_TRIPLE="$(cc -dumpmachine 2>/dev/null || echo x86_64-linux-gnu)"
    "$SRC_TREE/configure" \
        --build="$BUILD_TRIPLE" \
        --host="$TARGET_TRIPLE" \
        --target="$TARGET_TRIPLE" \
        --prefix=/usr \
        --sysconfdir=/etc \
        --localstatedir=/var \
        --with-sysroot=/ \
        --disable-werror --disable-nls \
        --disable-gdb --disable-gdbserver --disable-sim \
        --disable-multilib \
        CC="${TARGET_TRIPLE}-gcc" \
        CXX="${TARGET_TRIPLE}-g++" \
        AR="${TARGET_TRIPLE}-ar" \
        AS="${TARGET_TRIPLE}-as" \
        LD="${TARGET_TRIPLE}-ld" \
        NM="${TARGET_TRIPLE}-nm" \
        OBJCOPY="${TARGET_TRIPLE}-objcopy" \
        OBJDUMP="${TARGET_TRIPLE}-objdump" \
        RANLIB="${TARGET_TRIPLE}-ranlib" \
        STRIP="${TARGET_TRIPLE}-strip" \
        CFLAGS="-march=i486 -mtune=i486 -O2 -g" \
        CXXFLAGS="-march=i486 -mtune=i486 -O2 -g"

    echo "==> Building (-j $PARALLEL)"
    make -j "$PARALLEL"

    echo "==> Staging to $STAGE2_DESTDIR"
    rm -rf "$STAGE2_DESTDIR"
    mkdir -p "$STAGE2_DESTDIR"
    # The top-level binutils `install` target only walks the target-
    # tree subdirs (libiberty/, ...) when configured Canadian-cross
    # for substrate — it skips binutils/, gas/, ld/, libctf/ entirely
    # and `dist-toolchain/usr/bin/` ends up missing ld, as, objcopy,
    # objdump, readelf, strip, ar, nm, strings, c++filt, addr2line,
    # gprof, elfedit, size, ranlib.  `install-host` explicitly walks
    # all host subdirs and installs the binaries we actually want.
    make install-host DESTDIR="$STAGE2_DESTDIR"

    echo ""
    echo "==> Stage 2 complete."
    echo "    Substrate-ELF binaries staged at: $STAGE2_DESTDIR/usr/bin/"
    echo ""
    echo "    To inject into rootfs.img:"
    echo "        for f in $STAGE2_DESTDIR/usr/bin/*; do"
    echo "            debugfs -w -R \"write \$f /usr/bin/\$(basename \$f)\" \\"
    echo "                $SUBSTRATE_TOP/rootfs.img"
    echo "        done"
    exit 0
fi
