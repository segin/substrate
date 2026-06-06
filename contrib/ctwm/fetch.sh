#!/bin/sh
# contrib/ctwm/fetch.sh — download + verify + extract ctwm.
#
# ctwm (Claude's Tab Window Manager) extends twm with virtual screens,
# multiple workspaces, EWMH hints, XPM/title icons, etc.  CMake build;
# depends on the core X client libraries plus libXpm, and (on the build
# host) flex/bison + perl/sh for its parser and generated sources.
set -eu
VERSION="4.1.0"
TARBALL="ctwm-${VERSION}.tar.xz"
URL="https://www.ctwm.org/dist/${TARBALL}"
SHA256="dffc4724dda6d5637e96c44e476aee87850ff144312f589dd856e1e8bf192029"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/ctwm-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}";
    else wget -O "${TARBALL}" "${URL}"; fi
fi
echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -p1 --dry-run < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> applying ${p}"; patch -p1 < "${HERE}/patches/${p}"
        else echo "==> ${p} already applied (skipping)"; fi
    done < "${HERE}/series"
fi
echo "==> ctwm ${VERSION} ready at ${TREE_DIR}"
