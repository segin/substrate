#!/bin/sh
#
# contrib/xrdb/build.sh — cross-build xrdb for substrate.
# xrdb is the X resource database utility.  It reads resource files, runs
# them through cpp, and loads them into the X server's RESOURCE_MANAGER
# property.  CDE's dtsession_res (dtloadresources) pipes the CDE resource
# files through `/usr/bin/xrdb -merge`, so without it a CDE session starts
# into a bare desktop with no palette/font/appearance resources loaded.
# Produces /usr/bin/xrdb.
#
# Depends on the staged X client stack: xorgproto libXau xtrans libxcb
# libX11 libXmu (+ libXt/libICE/libSM pulled in by libXmu).
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-xrdb)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.2.2"
TREE_DIR="${HERE}/build/xrdb-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-xrdb}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Assemble dependency flags from every staged X dist tree.
PKGP=""; CPP=""; LDF=""
for dep in xorgproto xcb-proto libXau xtrans libxcb libX11 libXext libICE libSM libXt libXmu; do
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

# --with-cpp: xrdb invokes a C preprocessor at runtime to expand the CDE
# resource files (#include/#define).  The stage-2 toolchain installs cpp at
# /usr/bin/cpp on the target; give a fallback list so it still finds one.
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --with-cpp="/usr/bin/cpp,/lib/cpp,cpp" \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  xrdb staged under ${DESTDIR}"
