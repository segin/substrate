#!/bin/sh
set -eu

VERSION="1.4.21"
TARBALL="m4-${VERSION}.tar.xz"
URL="https://ftp.gnu.org/gnu/m4/${TARBALL}"
# https://ftp.gnu.org/gnu/m4/m4-1.4.21.tar.xz (GPG-verified against the .sig)
SHA256="f25c6ab51548a73a75558742fb031e0625d6485fe5f9155949d6486a2408ab66"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/m4-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
fi

echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

# Apply substrate patch series.
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
            echo "    (skip already-applied) ${p}"
            continue
        fi
        echo "    apply ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> Ready at ${TREE_DIR}"
