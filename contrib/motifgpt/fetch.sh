#!/bin/sh
# contrib/motifgpt/fetch.sh — fetch motifgpt, verify, extract, autoreconf, patch.
# motifgpt is a Motif GUI client for LLM chat APIs, built on Motif + libXt over
# disasterparty.  The upstream has no release tags, so we pin an exact commit
# (immutable).  The source archive ships no generated configure -> autoreconf.
set -eu
COMMIT="7d2cbc9a063cc12bcde93d7f2d0a0eef09a0cf72"
TARBALL="motifgpt-${COMMIT}.tar.gz"
URL="https://github.com/segin/motifgpt/archive/${COMMIT}.tar.gz"
SHA256="47c420ce523f56580f64f91940fddd1c26a1eaf61f42effd65454ff4cfff480c"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/motifgpt-${COMMIT}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
cd "${TREE_DIR}"
if [ -f "${HERE}/series" ]; then
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then echo "==> ${p} already applied"; continue; fi
        echo "==> Applying ${p}"; patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi
# AX_PTHREAD comes from autoconf-archive; aclocal pulls it from the host's
# /usr/share/aclocal during autoreconf.  No configure ships in the archive.
if [ ! -f configure ]; then
    echo "==> autoreconf --force --install"
    autoreconf --force --install
fi
for cs in $(find "${TREE_DIR}" -name config.sub); do
    grep -q substrate "$cs" || sed -i 's/\(-sortix\* \)/\1| -substrate* /; s/\(| sortix\* \)/\1| substrate* /' "$cs"
done
echo "==> motifgpt ${COMMIT} ready at ${TREE_DIR}"
