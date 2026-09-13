#!/bin/sh
# contrib/perl/build.sh — cross-build perl 5 for substrate with perl-cross.
#
# perl-cross builds a host miniperl first, then uses it to drive the target
# build; the substrate cross gcc is found from --target on PATH.
#
#   --targetarch=i386-substrate : skip perl-cross's config.sub guess, which
#                                 does not know substrate and stops with
#                                 "cannot determine target platform".
#   -Dosname=substrate          : a user -D wins over probes and hints, and
#                                 selects cnf/hints/substrate (installed by
#                                 fetch.sh from substrate.hints).
#
# perl builds in its source tree (perl-cross has no out-of-tree mode), so a
# rebuild starts from a fresh fetch.sh extraction.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; PKG="perl"; VERSION="5.44.0"
TREE="${HERE}/build/perl-${VERSION}"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${PKG}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -f "${TREE}/cnf/hints/substrate" ] || { echo "run ./fetch.sh first" >&2; exit 1; }

cd "${TREE}"
./configure --target=i386-unknown-substrate --targetarch=i386-substrate \
  -Dosname=substrate --prefix=/usr
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
# Stamp every ELF file: perl itself and the XS extension .so files.  perl
# installs the extensions read-only (0555), and dd cannot write through that,
# so lift the owner write bit for the stamp and put the mode back after.
find "${DESTDIR}/usr" -type f | while IFS= read -r f; do
    [ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = '177ELF' ] || continue
    if [ -w "$f" ]; then
        printf '\100' | dd of="$f" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
    else
        chmod u+w "$f"
        printf '\100' | dd of="$f" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
        chmod u-w "$f"
    fi
done
echo "==> ${PKG} staged under ${DESTDIR}"
