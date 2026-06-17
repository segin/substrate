#!/bin/sh
# contrib/tde/tdelibs/hostbuild.sh — build the BUILD-HOST (native x86-64)
# code-generation tools that tdelibs needs to run while cross-compiling.
#
# Several tdelibs build steps execute a freshly-built program ON THE HOST:
#   - tdeconfig_compiler   turns *.kcfg/*.kcfgc into C++ (dnssd, tdeutils)
#   - maketdewidgets       turns kde.widgets into tdewidgets.cpp
#   - dcopidl2cpp          DCOP stub/skel generation (built by contrib/tde/tqt3)
# A cross-built (substrate) binary cannot run here, so we build native ones.
#
# tdeconfig_compiler/maketdewidgets link libtdecore, which links libtqt
# (tqtinterface).  So this script first builds a native tqtinterface into
# the host TQt3 prefix, then configures tdelibs natively and builds just
# those two targets.  Results are dropped into the host TQt3 bin dir, which
# build.sh puts on PATH.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TDE_TOP="$(cd "${HERE}/.." && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
VER="14.1.6"
HB="${TDE_TOP}/tqt3/hostbuild/tqt-trinity-${VER}"   # native TQt3 (from tqt3 host build)
MODULES="${TDE_TOP}/tde-cmake/build/tde-cmake-trinity-${VER}/modules"
TQI_TREE="${TDE_TOP}/tqtinterface/build/tqtinterface-trinity-${VER}"
TDE_TREE="${HERE}/build/tdelibs-trinity-${VER}"

[ -x "${HB}/bin/tqmoc" ] || { echo "hostbuild.sh: native TQt3 missing — build contrib/tde/tqt3 host tools first (${HB})" >&2; exit 1; }
[ -d "${TQI_TREE}" ]    || { echo "hostbuild.sh: run contrib/tde/tqtinterface/fetch.sh first" >&2; exit 1; }
[ -d "${TDE_TREE}" ]    || { echo "hostbuild.sh: run ./fetch.sh first" >&2; exit 1; }

export PATH="${HB}/bin:${PATH}"
export PKG_CONFIG_PATH="${HB}/lib/pkgconfig:${HB}/lib"

# --- native tqtinterface (provides tqt.pc, libtqt, tmoc into the TQt3 prefix)
if [ ! -e "${HB}/lib/pkgconfig/tqt.pc" ] || [ ! -x "${HB}/bin/tmoc" ]; then
  echo "==> host tqtinterface"
  OBJ="${TDE_TOP}/tqtinterface/build/hostobj"
  rm -rf "${OBJ}"; mkdir -p "${OBJ}"; cd "${OBJ}"
  cmake -G "Unix Makefiles" \
      -DCMAKE_MODULE_PATH="${MODULES}" -DCMAKE_INSTALL_PREFIX="${HB}" \
      -DCMAKE_BUILD_TYPE=Release -DQT_PREFIX_DIR="${HB}" \
      -DMOC_EXECUTABLE="${HB}/bin/tqmoc" -DUIC_EXECUTABLE="${HB}/bin/tquic" \
      "${TQI_TREE}"
  make -j"${JOBS}"
  make install
fi

# tde_setup_dbus() in tdelibs' top CMakeLists demands a dbus-1-tqt.pc even
# though the host tools we build (tdeconfig_compiler/maketdewidgets) don't
# link dbus.  A benign stub lets configure proceed; the dbus targets are
# never built here.
if [ ! -e "${HB}/lib/pkgconfig/dbus-1-tqt.pc" ]; then
  cat > "${HB}/lib/pkgconfig/dbus-1-tqt.pc" <<'PC'
prefix=/opt/trinity-host
exec_prefix=${prefix}
libdir=${prefix}/lib
includedir=${prefix}/include/dbus-1-tqt

Name: dbus-tqt
Description: stub for native tdelibs host-tool build (dbus targets not built)
Version: 0.9.0
Libs:
Cflags:
PC
fi

# --- native tdelibs codegen tools
echo "==> host tdelibs codegen tools (tdeconfig_compiler, maketdewidgets)"
OBJ="${HERE}/hostbuild/obj"
rm -rf "${OBJ}"; mkdir -p "${OBJ}"; cd "${OBJ}"
cmake -G "Unix Makefiles" \
    -DCMAKE_MODULE_PATH="${MODULES}" -DCMAKE_INSTALL_PREFIX=/opt/trinity-host \
    -DQT_PREFIX_DIR="${HB}" \
    -DMOC_EXECUTABLE="${HB}/bin/tmoc" -DUIC_EXECUTABLE="${HB}/bin/tquic" \
    -DWITH_ARTS=OFF -DWITH_ALSA=OFF -DWITH_CUPS=OFF -DWITH_XRANDR=OFF \
    -DWITH_XCOMPOSITE=OFF -DWITH_INOTIFY=OFF -DWITH_TDEHWLIB=OFF -DWITH_ISPELL=ON \
    -DWITH_LIBART=OFF -DWITH_LIBIDN=OFF -DWITH_GAMIN=OFF -DWITH_PCRE2=OFF -DWITH_SSL=OFF \
    -DHAVE_GOOD_GETADDRINFO_EXITCODE=0 -DICEAUTH_PATH=/usr/bin/iceauth \
    -DINTLTOOL_MERGE_EXECUTABLE="${MODULES}/tde_l10n_merge.pl" \
    "${TDE_TREE}"
make -j"${JOBS}" tdeconfig_compiler maketdewidgets

# Stage the host tools next to the other host generators (build.sh adds
# ${HB}/bin to PATH).  Their RUNPATH already points at the native build
# trees + ${HB}/lib, so they self-resolve.
cp -f "${OBJ}/tdecore/tdeconfig_compiler/tdeconfig_compiler" "${HB}/bin/tdeconfig_compiler"
cp -f "${OBJ}/tdewidgets/maketdewidgets"                     "${HB}/bin/maketdewidgets"
echo "==> host tools staged in ${HB}/bin (tdeconfig_compiler, maketdewidgets)"
