#!/bin/sh
#
# contrib/mksh/fetch.sh — fetch the MirBSD Korn Shell tarball, verify,
# extract.  mksh is substrate's /bin/ksh and satisfies CDE's KSH
# build/runtime dependency (configure accepts ksh/ksh93/mksh).  The full
# ksh93 desktop shell (dtksh) is built separately from CDE's bundled ksh93.

set -eu

VERSION="R59c"
TARBALL="mksh-${VERSION}.tgz"
URL="http://www.mirbsd.org/MirOS/dist/mir/mksh/${TARBALL}"
SHA256="77ae1665a337f1c48c61d6b961db3e52119b38e58884d1c89684af31f87bc506"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/mksh"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}"
    else
        wget -O "${TARBALL}" "${URL}"
    fi
fi

echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

echo "==> mksh ${VERSION} ready at ${TREE_DIR}"
