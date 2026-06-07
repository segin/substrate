#!/bin/sh
#
# contrib/cde/build.sh — cross-configure (and, as prerequisites land, build)
# CDE for substrate.  Assembles a Motif + X11 + libXinerama sysroot and runs
# CDE's autotools configure.  Until the remaining prerequisite ports exist
# (libjpeg, Tcl, rpcgen, ksh, Sun RPC/ToolTalk — see README.SUBSTRATE.md),
# configure stops at the first unmet dependency; that is expected and the
# stop point advances as each port is added.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
TREE_DIR="${HERE}/build/cdesktopenv/cde"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

# Build-host programs CDE's configure requires (rpcgen, ksh, compress,
# sessreg, mkfontdir, bdftopcf, onsgmls).  hosttools/build.sh builds them
# from source into hosttools/prefix; prepend it (and the cross toolchain) to
# PATH so configure finds them.
HOSTTOOLS="${HERE}/hosttools/prefix/bin"
[ -x "${HOSTTOOLS}/rpcgen" ] || ( cd "${HERE}/hosttools" && ./build.sh )
PATH="${STAGE1_PREFIX}/bin:${HOSTTOOLS}:${PATH}"; export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Assemble the mini-sysroot: Motif + the X client stack + libXinerama +
# libjpeg + lmdb + Tcl, plus substrate's core libs (lmdb DT_NEEDEDs
# libpthread; configure link tests pull libc/libsys).
SR="${HERE}/build/sysroot"
rm -rf "${SR}"; mkdir -p "${SR}/usr/lib"
_have=0
for d in xorgproto libXau xtrans libxcb libX11 libXext libICE libSM \
         libXt libXmu libXpm libXaw libXinerama libjpeg lmdb tcl libtirpc motif; do
    st="${SUBSTRATE_TOP}/dist-${d}"
    [ -d "${st}/usr" ] || continue
    cp -a "${st}/usr/." "${SR}/usr/"
    _have=$((_have + 1))
done
[ "${_have}" -ge 18 ] || { echo "build.sh: only ${_have} dist trees found — build the X stack + Motif + libXinerama + libjpeg + lmdb + tcl first" >&2; exit 1; }
for l in c sys m pthread; do
    cp "${SUBSTRATE_TOP}/lib/${l}/lib${l}.so.0" "${SR}/usr/lib/" 2>/dev/null || true
done

BUILD_DIR="${HERE}/build/build-stage-substrate"
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> configure"
# crypt lives in substrate libc — do NOT let configure add -lcrypt.
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr/dt \
    CC=i386-unknown-substrate-gcc \
    CXX=i386-unknown-substrate-g++ \
    CPPFLAGS="-I${SR}/usr/include -I${SR}/usr/include/X11 -I${SR}/usr/include/tirpc" \
    LDFLAGS="-L${SR}/usr/lib -Wl,-rpath-link,${SR}/usr/lib" \
    --with-tcl="${SR}/usr/lib"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> CDE build complete (if you reached here, all prerequisites are in place)"
