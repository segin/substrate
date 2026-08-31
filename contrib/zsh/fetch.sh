#!/bin/sh
set -eu

VERSION="5.9"
TARBALL="zsh-${VERSION}.tar.xz"
URL="https://www.zsh.org/pub/zsh-${VERSION}.tar.xz"
# www.zsh.org/pub/ now carries only the current release -- 5.9 was removed
# from it and the primary URL 404s.  SourceForge still serves the pinned
# tarball, and SHA256 below proves it is the same file either way.
URL_FALLBACK="https://downloads.sourceforge.net/project/zsh/zsh/${VERSION}/zsh-${VERSION}.tar.xz"
# https://www.zsh.org/pub/zsh-5.9.tar.xz published SHA256
SHA256="9b8d1ecedd5b5e81fbf1918e876752a7dd948e05c1a0dba10ab863842d45acd5"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/zsh-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}" || curl -fSL -o "${TARBALL}" "${URL_FALLBACK}"
    else
        wget -O "${TARBALL}" "${URL}" || wget -O "${TARBALL}" "${URL_FALLBACK}"
    fi
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
