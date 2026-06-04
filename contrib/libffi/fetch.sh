#!/bin/sh
# contrib/libffi/fetch.sh — download + verify + extract + patch libffi.
# libffi is the mandatory FFI backend for GObject (GLib).  i386 sysv is a
# first-class libffi target, so this cross-builds cleanly with autotools.
set -eu
VERSION="3.4.6"
TARBALL="libffi-${VERSION}.tar.gz"
URL="https://github.com/libffi/libffi/releases/download/v${VERSION}/${TARBALL}"
SHA256="b0dea9df23c863a7a50e825440f3ebffabd65df1497108e5d437747843895a4e"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/libffi-${VERSION}"
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
        else
            echo "==> ${p} already applied (skipping)"
        fi
    done < "${HERE}/series"
fi
echo "==> libffi ${VERSION} ready at ${TREE_DIR}"
