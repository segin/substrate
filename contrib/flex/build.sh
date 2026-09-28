#!/bin/sh
#
# build.sh — configure + build + install flex for substrate.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.6.4"
TREE_DIR="${HERE}/build/flex-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-flex}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# libtool has no substrate branch and would build libfl static-only.
sh "${SUBSTRATE_TOP}/contrib/substrate-libtool-shared.sh" "${TREE_DIR}/configure"

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

# --disable-bootstrap: the bootstrap scanner (stage1flex) is a host
#   program, but it links with LIBS, and -lregex exists only for the
#   target.  The tarball ships a pregenerated scan.c, which is what
#   stage1flex would regenerate anyway.
# LIBS=-lregex: substrate keeps regcomp/regexec in libregex, not libc.
# ac_cv_path_M4: the m4 flex runs at scanner-generation time, on target
#   (contrib/m4).  The host m4 found at configure time would be baked in.
# ac_cv_func_*_0_nonnull: substrate's malloc(0)/realloc(NULL, 0) return
#   a real pointer; the cross default would pull in rpl_malloc.
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --disable-nls \
    --disable-bootstrap \
    ac_cv_path_M4=/usr/bin/m4 \
    ac_cv_func_malloc_0_nonnull=yes \
    ac_cv_func_realloc_0_nonnull=yes \
    LIBS=-lregex \
    CFLAGS="-O2 -g -march=i486 -mtune=i486"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
# POSIX lex, as most distributions ship it.
ln -sf flex "${DESTDIR}/usr/bin/lex"
printf '.so man1/flex.1\n' > "${DESTDIR}/usr/share/man/man1/lex.1"
find "${DESTDIR}" -name '*.la' -delete

echo "==> Done.  Staged at ${DESTDIR}/usr/bin/flex"
