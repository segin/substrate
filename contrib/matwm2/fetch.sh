#!/bin/sh
# contrib/matwm2/fetch.sh — fetch upstream master snapshot.
#
# matwm2 has no tagged releases; pin to a known-good commit SHA on
# master via the GitHub tarball download.

set -eu

# Pin to the master HEAD at the time of the substrate port.  Update
# both COMMIT and SHA256 when bumping.
COMMIT="master"
TARBALL="matwm2-${COMMIT}.tar.gz"
URL="https://github.com/segin/matwm2/archive/refs/heads/${COMMIT}.tar.gz"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/matwm2-${COMMIT}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    curl -fSL -o "${TARBALL}" "${URL}"
fi

# No SHA pin (master is a moving target); print the current SHA for the
# record so the operator can lock it down later if desired.
echo "==> Tarball SHA256:  $(sha256sum "${TARBALL}" | awk '{print $1}')"

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        if [ ! -f "${HERE}/patches/${p}" ]; then
            echo "fetch.sh: missing patch ${p}" >&2; exit 1
        fi
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> Patch ${p} already applied"; continue
        fi
        echo "==> Applying ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> matwm2 ${COMMIT} ready at ${TREE_DIR}"
