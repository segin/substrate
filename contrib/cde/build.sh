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

# Build IN-SOURCE.  CDE's Makefiles reference its exported Dt/* headers as
# -I../../include (relative to the build dir), so an out-of-source build can't
# find them — the headers live in the source tree.  Configure and make in
# TREE_DIR directly.
cd "${TREE_DIR}"

# Link-order fix: ttsession lists libtt before libstt, but libstt references
# libtt's API and both are static archives, so libtt must follow libstt.
# Append it (idempotent).
TTS="lib/tt/bin/ttsession/Makefile.am"
if [ -f "${TTS}" ] && ! grep -q 'lib/.libs/libtt.a' "${TTS}"; then
    sed -i 's@\(ttsession_LDADD = \$(LIBTT) \$(X_LIBS) \.\./\.\./slib/libstt\.a\)@\1 ../../../../lib/tt/lib/.libs/libtt.a@' "${TTS}"
fi

echo "==> configure"
# -D__linux__ -Dlinux: CDE has a Linux port and selects its modern code paths
# (vs old SVR4/SunOS) on these; substrate is pthread + ELF + glibc-like + BSD
# sockets, so the Linux paths are the right ones.  CDE guards on BOTH the
# modern __linux__ and the legacy `linux` predefine (e.g. `#ifndef linux`
# around the BSD-only SO_USELOOPBACK), and substrate's cross gcc defines
# neither, so define both.  Without them CDE pulls in legacy declarations that
# conflict with substrate's headers (its own extern ioctl, SO_USELOOPBACK, ...).
# crypt lives in substrate libc — do NOT let configure add -lcrypt.
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr/dt \
    CC=i386-unknown-substrate-gcc \
    CXX=i386-unknown-substrate-g++ \
    CPPFLAGS="-D__linux__ -Dlinux -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=return-mismatch -Wno-error=format -I${SR}/usr/include -I${SR}/usr/include/X11 -I${SR}/usr/include/tirpc" \
    LDFLAGS="-L${SR}/usr/lib -Wl,-rpath-link,${SR}/usr/lib" \
    --with-tcl="${SR}/usr/lib"

# In-tree generator tools: CDE compiles several small noinst_PROGRAMS and runs
# them mid-build to generate source (lineToData -> TermLineData.c, ...).  The
# cross-build compiles them for the target, so they can't execute on the build
# host (make fails with "Error 126").  Pre-build each with the host cc; the
# host object + binary are newer than their sources, so the subsequent cross
# make treats them as up-to-date and skips the target rebuild.  Their objects
# are noinst generators and never link into target artifacts, so a host object
# sitting in the tree is harmless.
HOST_GEN_TOOLS="
lib/DtTerm/util:lineToData
"
for entry in ${HOST_GEN_TOOLS}; do
    gdir=${entry%:*}; gtool=${entry#*:}
    echo "==> host-build ${gdir}/${gtool}"
    make -C "${gdir}" CC=cc CPPFLAGS= CFLAGS='-O2 -w' "${gtool}"
done

echo "==> make -j${JOBS}"
# GENCPP: CDE expands its *.cpp config templates with util/tradcpp, but the
# cross-build compiles that for the target (can't run on the build host).
# hosttools builds a host tradcpp; point every Makefile's GENCPP at it.  A
# command-line override propagates to all recursive sub-makes via MAKEFLAGS.
make -j"${JOBS}" GENCPP="${HOSTTOOLS}/tradcpp"

echo "==> CDE build complete (if you reached here, all prerequisites are in place)"
