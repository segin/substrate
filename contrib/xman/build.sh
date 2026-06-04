#!/bin/sh
#
# contrib/xman/build.sh — cross-build xman for substrate.
# xman is the Athena-widget manual-page browser.  Produces /usr/bin/xman.
#
# Depends on the staged X client stack: xorgproto xcb-proto libXau xtrans libxcb libX11 libXext libICE libSM libXt libXmu libXpm libXaw.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-xman)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.2.0"
TREE_DIR="${HERE}/build/xman-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-xman}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Assemble dependency flags from every staged X dist tree.
PKGP=""; CPP=""; LDF=""
for dep in xorgproto xcb-proto libXau xtrans libxcb libX11 libXext libICE libSM libXt libXmu libXpm libXaw; do
    st="${SUBSTRATE_TOP}/dist-${dep}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/share/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/share/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
done
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"

export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF} -Wl,--copy-dt-needed-entries"

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# mandoc uses the BSD-style man.conf; xman has no substrate case in its
# man-config-format switch, so fold substrate into the *bsd* branch.
sed -i 's/\*bsd\* )/*bsd* | *substrate* )/' "${TREE_DIR}/configure"
echo "==> configure"
# xman probes for a man-config file with AC_CHECK_FILE, which autoconf cannot
# run when cross-compiling.  Seed the cache: substrate's man toolchain is
# mandoc reading /etc/man.conf, so point xman at that and answer the rest no.
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    ac_cv_file__etc_man_conf=yes \
    ac_cv_file__etc_man_config=no \
    ac_cv_file__etc_manpath_config=no \
    ac_cv_file__usr_share_misc_man_conf=no \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -include strings.h"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  xman staged under ${DESTDIR}"
