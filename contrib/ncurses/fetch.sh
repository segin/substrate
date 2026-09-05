#!/bin/sh
set -eu

VERSION="6.4"
TARBALL="ncurses-${VERSION}.tar.gz"
URL="https://invisible-mirror.net/archives/ncurses/${TARBALL}"
# ftp.gnu.org carries the byte-identical tarball (same SHA256), and
# invisible-mirror has served a 7 KB error page with HTTP 200 instead of the
# archive -- which -f does not catch, only the checksum below did.
URL_FALLBACK="https://ftp.gnu.org/gnu/ncurses/${TARBALL}"
SHA256="6931283d9ac87c5073f30b6290c4c75f21632bb4fc3603ac8100812bed248159"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/ncurses-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# A tarball already present but wrong is worse than none: it fails the
# checksum on every later run and never re-downloads.  Drop it first.
if [ -f "${TARBALL}" ] && ! echo "${SHA256}  ${TARBALL}" | sha256sum -c --status -; then
    echo "==> ${TARBALL} fails its checksum — discarding and refetching"
    rm -f "${TARBALL}"
fi

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    for u in "${URL}" "${URL_FALLBACK}"; do
        echo "==> Fetching ${u}"
        if command -v curl >/dev/null 2>&1; then
            curl -fSL -o "${TARBALL}" "${u}" || continue
        else
            wget -O "${TARBALL}" "${u}" || continue
        fi
        # Accept the first source that actually serves the archive.
        echo "${SHA256}  ${TARBALL}" | sha256sum -c --status - && break
        echo "    (wrong content from ${u}, trying the next source)"
        rm -f "${TARBALL}"
    done
fi
[ -f "${TARBALL}" ] || { echo "fetch.sh: could not download ${TARBALL}" >&2; exit 1; }

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
            echo "    (skip already-applied) ${p}"
            continue
        fi
        echo "    apply ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> Ready at ${TREE_DIR}"
