#!/bin/sh
# contrib/hexchat/fetch.sh — HexChat 2.10.2 (GTK+ 2 IRC client; last autotools release).
set -eu
VERSION="2.10.2"
TARBALL="hexchat-${VERSION}.tar.xz"
URL="https://dl.hexchat.net/hexchat/${TARBALL}"
SHA256="87ebf365c576656fa3f23f51d319b3a6d279e4a932f2f8961d891dd5a5e1b52c"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/hexchat-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
if [ "${SHA256}" != "REPLACE" ]; then echo "${SHA256}  ${TARBALL}" | sha256sum -c -; fi
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }

# Apply the substrate patch series (idempotent: skip already-applied).
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
        echo "==> Applying ${p}"; patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
    cd "${BUILD_DIR}"
fi

. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> hexchat ${VERSION} ready at ${TREE_DIR}"
