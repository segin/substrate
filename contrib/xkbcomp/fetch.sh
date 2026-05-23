#!/bin/sh
# contrib/xkbcomp/fetch.sh

set -eu

VERSION="1.4.7"
TARBALL="xkbcomp-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/app/${TARBALL}"
SHA256="0a288114e5f44e31987042c79aecff1ffad53a8154b8ec971c24a69a80f81f77"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/xkbcomp-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}"
    else
        wget -O "${TARBALL}" "${URL}"
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

# Add substrate to configure libtool dispatch (same pattern as pixman).
cd "${TREE_DIR}"
if ! grep -q 'substrate\*' configure 2>/dev/null; then
    echo "==> Adding substrate* to configure libtool dispatch"
    sed -i \
        -e 's/linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\*/linux* | k*bsd*-gnu | kopensolaris*-gnu | gnu* | substrate*/g' \
        -e 's/gnu\* | linux\* | tpf\* | k\*bsd\*-gnu | kopensolaris\*-gnu/gnu* | linux* | tpf* | k*bsd*-gnu | kopensolaris*-gnu | substrate*/g' \
        -e 's/^    linux\*)$/    linux* | substrate*)/' \
        configure
fi

echo "==> xkbcomp ${VERSION} ready at ${TREE_DIR}"
