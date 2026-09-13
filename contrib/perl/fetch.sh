#!/bin/sh
# contrib/perl/fetch.sh — fetch perl and perl-cross, verify, extract, overlay,
# apply patches.
#
# perl's own Configure cannot cross-compile without running target programs.
# perl-cross replaces it with a configure that only needs to compile and
# link, and ships per-version diffs for the parts of the perl tree that
# assume otherwise; it is unpacked over the perl source tree.
set -eu
VERSION="5.44.0"
TARBALL="perl-${VERSION}.tar.xz"
URL="https://www.cpan.org/src/5.0/${TARBALL}"
SHA256="505cf43912e9480495c344c70260452e32aa2a73c546a026b3f100053b23ce91"
PC_VERSION="1.6.5"
PC_TARBALL="perl-cross-${PC_VERSION}.tar.gz"
PC_URL="https://github.com/arsv/perl-cross/releases/download/${PC_VERSION}/${PC_TARBALL}"
PC_SHA256="81130cd4b8c6d9eb2a1959f37d44391a46ad6a6794fc41fed5441f74e85e3dd0"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/perl-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
fetch() {
    [ -f "$1" ] && return 0
    [ "${NO_NETWORK:-}" = 1 ] && { echo "missing $1" >&2; exit 1; }
    curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "$1" "$2"
}
[ "${1:-}" = "--no-network" ] && NO_NETWORK=1
fetch "${TARBALL}" "${URL}"
fetch "${PC_TARBALL}" "${PC_URL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
echo "${PC_SHA256}  ${PC_TARBALL}" | sha256sum -c -
if [ ! -d "${TREE}" ]; then
    tar xf "${TARBALL}"
    tar xf "${PC_TARBALL}"
    # perl-cross's tarball unpacks to perl-cross-X/; its files go on top of
    # the perl tree (cnf/, configure, Makefile, miniperl_top, ...).
    cp -R "perl-cross-${PC_VERSION}/." "${TREE}/"
    rm -rf "perl-cross-${PC_VERSION}"
fi
# Selected by osname; see the comment at the top of substrate.hints.
cp "${HERE}/substrate.hints" "${TREE}/cnf/hints/substrate"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "perl ${VERSION} (perl-cross ${PC_VERSION}) ready"
