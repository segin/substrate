#!/bin/sh
# contrib/glib/fetch.sh — download + verify + extract + patch GLib.
# GLib 2.56.4 is the LAST autotools release (2.58+ is meson-only); substrate
# has no meson/ninja cross-build, so 2.56.x is the target.  Depends on libffi
# (contrib/libffi) for GObject; PCRE is bundled via --with-pcre=internal.
set -eu
VERSION="2.56.4"
SERIES="2.56"
TARBALL="glib-${VERSION}.tar.xz"
URL="https://download.gnome.org/sources/glib/${SERIES}/${TARBALL}"
SHA256="27f703d125efb07f8a743666b580df0b4095c59fc8750e8890132c91d437504c"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/glib-${VERSION}"
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
# Teach the bundled config.sub about the substrate OS (same one-token shape
# used across contrib).  glib's config.sub vintage uses the older list.
for cs in "${TREE_DIR}/config.sub"; do
    [ -f "$cs" ] || continue
    grep -q 'substrate' "$cs" || \
      sed -i 's/\(-sortix\* \)/\1| -substrate* /; s/\(| sortix\* \)/\1| substrate* /' "$cs"
done
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
echo "==> glib ${VERSION} ready at ${TREE_DIR}"
