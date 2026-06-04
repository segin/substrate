#!/bin/sh
# contrib/cjson/fetch.sh — fetch cJSON, verify, extract, patch.
# cJSON is a single-translation-unit JSON parser (cJSON.c + cJSON.h, plus the
# optional cJSON_Utils pointer/patch helper).  Needed by disasterparty and
# motifgpt.
set -eu
VERSION="1.7.19"
TARBALL="cjson-${VERSION}.tar.gz"
URL="https://github.com/DaveGamble/cJSON/archive/refs/tags/v${VERSION}.tar.gz"
SHA256="7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/cJSON-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then echo "==> ${p} already applied"; continue; fi
        echo "==> Applying ${p}"; patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi
echo "==> cjson ${VERSION} ready at ${TREE_DIR}"
