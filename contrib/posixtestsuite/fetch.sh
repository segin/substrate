#!/bin/sh
#
# contrib/posixtestsuite/fetch.sh — fetch the Open POSIX Test Suite.
#
# OPTS is maintained inside the Linux Test Project (LTP) at
# testcases/open_posix_testsuite/.  We do a shallow, blobless, sparse
# clone of LTP and check out only that subdirectory, then apply the
# substrate patch series.
#
# Usage:  ./fetch.sh [--no-network]

set -eu

LTP_URL="https://github.com/linux-test-project/ltp.git"
# Pinned LTP commit (the OPTS subtree we validated against).
LTP_COMMIT="01d0eecd694cab3b95db1394e327bdd40974493e"
SUBDIR="testcases/open_posix_testsuite"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE="${BUILD_DIR}/ltp"

mkdir -p "${BUILD_DIR}"

if [ ! -d "${TREE}/${SUBDIR}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: LTP tree missing" >&2; exit 1; }
    echo "==> Cloning LTP (shallow, blobless, sparse -> ${SUBDIR})"
    rm -rf "${TREE}"
    git clone --filter=blob:none --no-checkout "${LTP_URL}" "${TREE}"
    cd "${TREE}"
    git sparse-checkout init --cone
    git sparse-checkout set "${SUBDIR}"
    # Pin to the validated commit if it is reachable; otherwise take the
    # default branch tip (LTP prunes shallow history aggressively).
    if git fetch --depth 1 origin "${LTP_COMMIT}" >/dev/null 2>&1; then
        git checkout "${LTP_COMMIT}"
    else
        echo "==> pinned commit not fetchable, using default branch tip"
        git checkout HEAD
    fi
else
    echo "==> LTP OPTS tree already present at ${TREE}/${SUBDIR}"
fi

# Apply the substrate patch series (if any).
if [ -f "${HERE}/series" ]; then
    cd "${TREE}/${SUBDIR}"
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> Patch ${p} already applied"; continue
        fi
        echo "==> Applying ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> OPTS ready at ${TREE}/${SUBDIR}"
