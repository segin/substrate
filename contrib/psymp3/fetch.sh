#!/bin/sh
# contrib/psymp3/fetch.sh — fetch the PsyMP3 release tarball, verify, extract,
# apply patches.  Upstream publishes no source tarball of its own, so this is
# GitHub's archive of the release tag; it has no generated configure (build.sh
# runs autogen.sh on the build host).
set -eu
VERSION="2.4.1-RELEASE"
TARBALL="psymp3-${VERSION}.tar.gz"
URL="https://github.com/segin/psymp3/archive/refs/tags/${VERSION}.tar.gz"
SHA256="9728ebb2444305f5ff29984fd57014ee09eb6aee5e32b51196d711ebe20a4be0"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/psymp3-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    . "${HERE}/../substrate-fetch.sh"
    substrate_fetch "${URL}" "${TARBALL}" "${SHA256}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
if [ -f "${HERE}/series" ]; then
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -d "${TREE_DIR}" -p1 --dry-run -R -s -f < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> ${p} already applied"; continue
        fi
        echo "==> Applying ${p}"; patch -d "${TREE_DIR}" -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi
echo "==> PsyMP3 ${VERSION} ready at ${TREE_DIR}"
