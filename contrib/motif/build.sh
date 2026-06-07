#!/bin/sh
# contrib/motif/build.sh — cross-build OpenMotif 2.3.8 for substrate.
# Produces the Motif libraries libXm (widgets) + libMrm (resource manager)
# and their headers.  The clients (mwm, uil, xmbind) are out of scope — they
# need the wml/uil build-time tool generators that the cross make mis-links.
# Depends on the staged X toolkit: libX11, libXext, libXt, libXmu, libXpm.
# Built --disable-xft (no Xft/fontconfig port yet): core X bitmap fonts only.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.3.8"
TREE_DIR="${HERE}/build/motif-${VERSION}"
BUILD_DIR="${TREE_DIR}"     # Motif's autotools build is in-tree
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-motif}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

PKGP=""; CPP=""; LDF=""
for d in xorgproto libXau xtrans libxcb libX11 libXext libICE libSM libXt libXmu libXpm; do
    st="${SUBSTRATE_TOP}/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
done
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"
export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP} -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"
export LDFLAGS="${LDF} -Wl,--copy-dt-needed-entries"
# Expose libregex (substrate's POSIX regcomp/regexec) to configure's link
# probes so the AC_CHECK_FUNCS([regcomp]) test passes.  Otherwise Motif defines
# NO_REGCOMP and falls back to the legacy SysV regcmp/regex API, which substrate
# does not provide -- leaving libXm with undefined regcmp/regex references.
export LIBS="-lregex"

cd "${BUILD_DIR}"
[ -f Makefile ] && make distclean >/dev/null 2>&1 || true
CACHE="${BUILD_DIR}/substrate.cache"
grep -v '^ac_cv_env_' "${HERE}/substrate-cross.cache" > "${CACHE}"
# Teach Motif's libtool that substrate builds ELF shared libraries so
# --enable-shared yields libXm.so / libMrm.so, not just the .a archives.
sh "${HERE}/../substrate-libtool-shared.sh" ./configure
echo "==> configure"
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --disable-xft \
    --cache-file="${CACHE}" \
    --disable-printing \
    CC=i386-unknown-substrate-gcc \
    CXX=i386-unknown-substrate-g++ \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -std=gnu89 -Wno-error" \
    CXXFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

# Motif builds host-side tools (config/util/makestrs &c) with $(CC); configure
# set $(CC) to the cross compiler, so those generators came out as substrate
# binaries that can't run on the build host (Error 126).  Do a first pass, then
# rebuild the generators with the host compiler (newest timestamps so the
# resumed make won't relink them for the target), then finish.
echo "==> make -j${JOBS} (pass 1)"
make -j"${JOBS}" || true
echo "==> rebuild build-time tools with the host compiler"
gcc -O2 -w -std=gnu89 -c config/util/makestrs.c -o config/util/makestrs.o
gcc -O2 -w -std=gnu89 config/util/makestrs.o -o config/util/makestrs
touch config/util/makestrs.o config/util/makestrs

# We ship only the LIBRARIES (libXm + libMrm) and their headers — these are the
# Motif deliverable that client programs link against.  The clients (mwm, uil,
# xmbind &c) and the wml/uil build-time tools under tools/ are not built: wml's
# flex/yacc generators are themselves build-host programs that the cross make
# mis-links for the target, and no library depends on their output (the UIL
# tables are pre-generated in the tarball).  Build lib/Xm and lib/Mrm directly.
echo "==> make -j${JOBS} libraries (lib/Xm, lib/Mrm)"
make -j"${JOBS}" -C lib/Xm
make -j"${JOBS}" -C lib/Mrm

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make -C lib/Xm   install DESTDIR="${DESTDIR}"
make -C lib/Mrm  install DESTDIR="${DESTDIR}"
make -C bindings install DESTDIR="${DESTDIR}"   # xmbind.alias virtual-key data
make -C bitmaps  install DESTDIR="${DESTDIR}"   # xm_hour*/xm_error/... X bitmaps (CDE DtSvc includes them)
rm -f "${DESTDIR}"/usr/lib/*.la
echo "==> Done.  Motif libraries staged at ${DESTDIR}/usr/{lib/lib{Xm,Mrm}.a,include/{Xm,Mrm}}"
