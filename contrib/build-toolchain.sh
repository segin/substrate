#!/bin/sh
#
# build-toolchain.sh — fetch, patch, and build the substrate toolchain.
#
# Loops over the components in $COMPONENTS (default: binutils gcc) and
# runs each one's fetch.sh and build.sh in order.  Stage 2 (native-on-
# substrate, --prefix=/usr) requires stage 1 of gcc to be installed
# first, so the natural ordering is:
#
#   1. binutils stage 1   (cross binutils on Linux)
#   2. gcc      stage 1   (cross gcc on Linux, uses binutils stage 1)
#   3. binutils stage 2   (substrate-ELF binutils, --prefix=/usr)
#   4. gcc      stage 2   (substrate-ELF gcc, --prefix=/usr)
#
# Components that don't exist yet (no contrib/<name>/build.sh) are
# skipped with a notice.
#
# Env:
#   COMPONENTS         space-separated list, default "binutils gcc"
#   STAGES             space-separated list, default "1 2"
#   STAGE1_PREFIX      passed through to each build.sh
#   STAGE2_DESTDIR     passed through to each build.sh
#
# Usage:
#   ./build-toolchain.sh                      # everything
#   ./build-toolchain.sh --stage=1            # cross-toolchain only
#   ./build-toolchain.sh --stage=2            # native build (needs stage 1)
#   COMPONENTS=binutils ./build-toolchain.sh  # binutils only
#

set -eu

STAGES="${STAGES:-1 2}"
COMPONENTS="${COMPONENTS:-binutils gcc}"

for arg in "$@"; do
    case "$arg" in
        --stage=*) STAGES="${arg#--stage=}" ;;
        -h|--help)
            sed -n '/^# build-toolchain/,/^# Usage:/p; /^# Usage:/,/^$/p' "$0"
            exit 0
            ;;
        *) echo "build-toolchain.sh: unknown arg $arg" >&2; exit 2 ;;
    esac
done

HERE="$(cd "$(dirname "$0")" && pwd)"
export STAGE1_PREFIX="${STAGE1_PREFIX:-/opt/substrate-toolchain}"

# Ensure stage-1 bins are on PATH so each successive component finds the
# tools it needs (binutils for gcc; both for stage 2).
export PATH="${STAGE1_PREFIX}/bin:${PATH}"

# Stage 1 must run for every component before stage 2 runs for anything.
# This isn't a fold per (component, stage) — it's two outer passes.
for stage in $STAGES; do
    echo ""
    echo "############################################################"
    echo "## STAGE $stage"
    echo "############################################################"

    for c in $COMPONENTS; do
        dir="$HERE/$c"
        if [ ! -d "$dir" ]; then
            echo ""
            echo "==> SKIP: $c (no contrib/$c/ — patches not vendored yet)"
            continue
        fi
        if [ ! -x "$dir/build.sh" ]; then
            echo ""
            echo "==> SKIP: $c (no build.sh)"
            continue
        fi

        # fetch.sh is idempotent and cheap (verifies sha256, skips
        # extract if tree is already there).  Always invoke it; if
        # the tree exists, sleeps less than a second.
        if [ -x "$dir/fetch.sh" ]; then
            if ! ls -d "$dir/build/${c}"-*/ >/dev/null 2>&1; then
                echo ""
                echo "==> Fetching $c"
                (cd "$dir" && ./fetch.sh)
            fi
        fi

        echo ""
        echo "==> Building $c (stage $stage)"
        (cd "$dir" && ./build.sh --stage="$stage")
    done
done

echo ""
echo "############################################################"
echo "## Done."
echo "############################################################"
echo ""
echo "Stage 1 (cross-toolchain) installed under: $STAGE1_PREFIX"
echo "Stage 2 (substrate-ELF binaries) staged under: ${STAGE2_DESTDIR:-${HERE}/../dist-toolchain}"
echo ""
echo "Inject stage 2 into rootfs:"
echo "  find \${STAGE2_DESTDIR}/usr -type f | while read f; do ... debugfs -w ... ; done"
