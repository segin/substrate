#!/bin/sh
#
# contrib/luit/fetch.sh — fetch the luit tarball, verify, extract,
# apply the substrate patch series.
#
# luit is the Unicode/locale ISO-2022 filter from Thomas Dickey's
# tree on invisible-island (the same source xterm is built from).
# It bridges legacy charset terminals to UTF-8 locales and is
# typically invoked by xterm as `xterm -lc` when the user locale
# doesn't match the terminal's native encoding.

set -eu

VERSION="20250912"
TARBALL="luit-${VERSION}.tgz"
URL="https://invisible-island.net/archives/luit/${TARBALL}"
SHA256="46958060e66f35bcb8a51ba22da1c13d726d28a86c1cf520511bcf7914bef39e"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/luit-${VERSION}"

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

echo "==> luit ${VERSION} ready at ${TREE_DIR}"
