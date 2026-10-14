#!/bin/sh
#
# contrib/xtrans/fetch.sh — fetch the X transport-layer tarball,
# verify, extract, apply the substrate patch series.

set -eu

VERSION="1.6.0"
TARBALL="xtrans-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/lib/${TARBALL}"
SHA256="faafea166bf2451a173d9d593352940ec6404145c5d1da5c213423ce4d359e92"

# The mirrors of X.Org's tree stop at 1.5.2, and freedesktop.org has
# been unreachable from the CI machines; Debian's copy is the same
# release gzipped (the tar inside each is byte for byte the other's:
# 22b27bb60561d4d9ba90ea5e0c397860370d5595c0906dd71557b51309ce4a84).
ALT_TARBALL="xtrans-${VERSION}.tar.gz"
ALT_URL="https://deb.debian.org/debian/pool/main/x/xtrans/xtrans_${VERSION}.orig.tar.gz"
ALT_SHA256="936b74c60b19c317c3f3cb1b114575032528dbdaf428740483200ea874c2ca0a"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/xtrans-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ] && [ -f "${ALT_TARBALL}" ]; then
    TARBALL="${ALT_TARBALL}"
    SHA256="${ALT_SHA256}"
fi
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    . "${HERE}/../substrate-fetch.sh"
    if ! substrate_fetch "${URL}" "${TARBALL}" "${SHA256}"; then
        echo "==> Fetching ${ALT_URL}"
        substrate_fetch "${ALT_URL}" "${ALT_TARBALL}" "${ALT_SHA256}"
        TARBALL="${ALT_TARBALL}"
        SHA256="${ALT_SHA256}"
    fi
fi

echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        case "$p" in
            ''|'#'*) continue ;;
        esac
        if [ ! -f "${HERE}/patches/${p}" ]; then
            echo "fetch.sh: missing patch ${p}" >&2
            exit 1
        fi
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> Patch ${p} already applied"
            continue
        fi
        echo "==> Applying ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> xtrans ${VERSION} ready at ${TREE_DIR}"
