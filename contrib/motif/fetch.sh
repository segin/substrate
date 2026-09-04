#!/bin/sh
# contrib/motif/fetch.sh — fetch OpenMotif 2.3.8, verify, extract, patch.
set -eu
VERSION="2.3.8"
TARBALL="motif-${VERSION}.tar.gz"
URL="https://downloads.sourceforge.net/project/motif/Motif%20${VERSION}%20Source%20Code/${TARBALL}"
SHA256="859b723666eeac7df018209d66045c9853b50b4218cecadb794e2359619ebce7"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/motif-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
# Teach the bundled config.sub about the substrate OS triplet.
#
# Three layouts, because config.sub's OS allowlist has been rewritten twice:
# the modern undashed one-per-line form, the 2015-era dashed list with a
# sortix token, and -- what Motif 2.3.8 actually ships, timestamp 2014-12-03
# -- a dashed list from before sortix existed at all.  Anchoring only on
# sortix matched nothing there and quietly did nothing, so configure died
# much later with the far less obvious
#
#     Invalid configuration `i386-unknown-substrate': system `substrate'
#     not recognized
#
# Then ASSERT it worked: a sed that matches nothing is silent, and that is
# the whole failure mode being fixed here.
for cs in $(find "${TREE_DIR}" -name config.sub); do
    if ! grep -q substrate "$cs"; then
        sed -i -e 's/\(| sortix\* \)/\1| substrate* /' \
               -e 's/\(| -sortix\* \)/\1| -substrate* /' \
               -e 's/^\(\t *| -aos\* | -aros\* \)/\t      | -substrate* \\\n\1/' \
               "$cs"
    fi
    if ! sh "$cs" i386-unknown-substrate >/dev/null 2>&1; then
        echo "fetch.sh: $cs still rejects i386-unknown-substrate" >&2
        echo "          (its OS allowlist has a layout none of the above matched)" >&2
        exit 1
    fi
done
if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then echo "==> ${p} already applied"; continue; fi
        echo "==> Applying ${p}"; patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi
echo "==> motif ${VERSION} ready at ${TREE_DIR}"
