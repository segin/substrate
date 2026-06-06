#!/bin/sh
# contrib/pkg-config/fetch.sh — download + verify + extract + patch pkg-config.
#
# freedesktop pkg-config 0.29.2 — the last release.  Built --with-internal-glib
# so it bootstraps without an external glib (the bundled glib 2.x subset is
# cross-compiled alongside it), and teaches the substrate cross toolchain to
# emit CFLAGS/LIBS from the /usr/lib/pkgconfig/*.pc files that the contrib ports
# install.
set -eu
VERSION="0.29.2"
TARBALL="pkg-config-${VERSION}.tar.gz"
URL="https://pkg-config.freedesktop.org/releases/${TARBALL}"
SHA256="6fc69c01688c9458a57eb9a1664c9aba372ccda420a02bf4429fe610e7e7d591"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/pkg-config-${VERSION}"
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
# Teach every bundled config.sub about the substrate OS token (the top-level one
# and the internal glib's copy).  pkg-config 0.29.2 ships a 2015-vintage
# config.sub whose OS-name whitelist uses the leading-dash form (`-aros*`);
# newer vintages elsewhere in contrib use the dashless form (`sortix*`).
# Handle both, idempotently.
for cs in $(find "${TREE_DIR}" -name config.sub); do
    grep -q 'substrate' "$cs" && continue
    sed -i 's/\(sortix\* \)/\1| substrate* /' "$cs"          # newer dashless list
    grep -q 'substrate' "$cs" || \
      sed -i 's/\(-aros\* \)/\1| -substrate* /' "$cs"        # 2015 leading-dash list
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
echo "==> pkg-config ${VERSION} ready at ${TREE_DIR}"
