#!/bin/sh
# contrib/tde/tqt3/build.sh — cross-compile TQt3 for substrate.
#
# TQt3 keeps the classic Qt3 build system: configure builds the host
# tqmake/tqmoc, then the libraries are compiled for the -xplatform
# target.  We use a substrate-g++ mkspec (cross gcc + substrate sysroot)
# as the cross target and linux-g++ (host gcc) as the build platform.
#
# uic (tquic) is the exception: configure only auto-host-builds anything
# whose .pro path matches "*moc*", so uic would be cross-compiled to a
# substrate ELF that cannot run on the build host.  uic also links the
# full -ltqt-mt (unlike the standalone moc), so we cannot bootstrap it
# the way moc is.  We therefore do a small NATIVE host TQt3 build first
# (hostbuild/) purely to produce a runnable host tquic, make it
# relocatable, and drop it into the cross tree's bin/ — the cross
# configure already points QMAKE_UIC at $build/bin/tquic.
#
# Only the library + plugins are cross-built (sub-tools/sub-tutorial/
# sub-examples are GUI apps TDE does not need; tdelibs needs only the
# library, headers, and the host moc/uic/qmake).
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
VERSION="14.1.6"
TREE="${HERE}/build/tqt-trinity-${VERSION}"
HOSTTREE="${HERE}/hostbuild/tqt-trinity-${VERSION}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-tqt3}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

CONFIGURE_COMMON="-prefix /opt/trinity -thread -no-exceptions \
    -disable-opengl -no-cups -no-nis -no-sm \
    -xft -no-xrandr -no-xinerama -no-xcursor -no-xkb \
    -qt-gif -qt-libpng -system-libjpeg -no-imgfmt-mng -qt-zlib"

# Where the HOST's X libraries actually live.
#
# TQt3's config.tests/x11/*.test are filesystem probes, not compile tests.
# Each one looks for its library in a hardcoded
#
#     LIBDIRS="$IN_LIBDIRS $XDIRS /usr/shlib /usr/lib /lib"
#
# where $XDIRS is QMAKE_LIBDIR_X11 from the mkspec -- /usr/X11R6/lib for
# linux-g++, a path no distribution has used in twenty years.  So the host
# build effectively probes /usr/lib alone.  On Arch that is where libXft
# and libXrender are and everything works; on a multiarch distribution
# (Debian, Ubuntu) they are in /usr/lib/<triple> and every probe misses.
#
# configure then turns Xrender off, which gates the Xft block, which turns
# freetype off -- silently, all three.  TQt3 does not survive its own
# answer: qfontdatabase_x11.cpp compiles the Xft code regardless of the
# QT_NO_XFTFREETYPE that results, so the build dies with
#
#     error: 'XftPatternGetString' was not declared in this scope
#     error: 'FcFontSetDestroy' was not declared in this scope
#
# configure's -L feeds IN_LIBDIRS, which is searched first.  Ask pkg-config
# where libXft is rather than guessing a layout; -print-multiarch is the
# fallback and is empty on non-multiarch distributions, where /usr/lib is
# already right.
#
# The CROSS configure needs none of this: mkspecs/substrate-g++ sets
# QMAKE_LIBDIR_X11 and QMAKE_INCDIR_X11 to the sysroot, so $XDIRS covers it.
host_probe_flags() {
    _cands="$(pkg-config --variable=libdir xft 2>/dev/null || true)"
    _cands="${_cands} $(pkg-config --variable=libdir xrender 2>/dev/null || true)"
    _ma="$(gcc -print-multiarch 2>/dev/null || true)"
    if [ -n "${_ma}" ]; then
        _cands="${_cands} /usr/lib/${_ma}"
    fi
    # Deduplicate: these end up in QMAKE_LIBDIR_FLAGS, i.e. on every link
    # line, and the two pkg-config answers are usually the same directory.
    _seen=""
    for _d in ${_cands}; do
        [ -d "${_d}" ] || continue
        case " ${_seen} " in *" ${_d} "*) continue ;; esac
        _seen="${_seen} ${_d}"
        printf ' -L%s' "${_d}"
    done
    :
}
HOST_PROBE_FLAGS="$(host_probe_flags)"

# Make the host tquic relocatable: RUNPATH=$ORIGIN with libtqt-mt.so.3
# sitting next to the binary.  (The default RUNPATH /opt/trinity-host/lib
# is long enough for chrpath to overwrite with the shorter $ORIGIN.)
reloc_tquic() {  # $1 = dest bin dir
    d="$1"
    cp "${HOSTTREE}/lib/libtqt-mt.so.3."* "${d}/" 2>/dev/null || true
    so="$(cd "${d}" && ls libtqt-mt.so.3.* 2>/dev/null | head -1)"
    [ -n "${so}" ] && ln -sf "${so}" "${d}/libtqt-mt.so.3"
    # Called once on HOSTTREE/bin itself (to make the host tquic relocatable)
    # and again on TREE/bin (to drop it over the cross-built one).  In the
    # first case source and destination are the SAME FILE: cp refuses, and
    # with set -e that took the whole build down -- so the port could only
    # ever be built once, from a clean hostbuild/, and every re-run died
    # here before compiling anything.
    if [ "${HOSTTREE}/bin/tquic" != "${d}/tquic" ]; then
        cp "${HOSTTREE}/bin/tquic" "${d}/tquic"
    fi
    chrpath -r '$ORIGIN' "${d}/tquic" >/dev/null 2>&1 || true
}

# ---------------------------------------------------------------------------
# Phase 1: native host build, just enough to produce a runnable host tquic.
# ---------------------------------------------------------------------------
if [ ! -x "${HOSTTREE}/bin/tquic" ]; then
    echo "==> host TQt3 build (for tquic)"
    if [ ! -d "${HOSTTREE}" ]; then
        TB="$(ls "${HERE}/build/"*.tar.* 2>/dev/null | head -1)"
        [ -n "${TB}" ] || { echo "build.sh: tarball missing; run ./fetch.sh" >&2; exit 1; }
        mkdir -p "${HERE}/hostbuild"; ( cd "${HERE}/hostbuild" && tar xf "${TB}" )
    fi
    ( cd "${HOSTTREE}"
      export TQTDIR="${HOSTTREE}" PATH="${HOSTTREE}/bin:${PATH}"
      echo "    host X probe dirs:${HOST_PROBE_FLAGS:- (none needed)}"
      yes yes | ./configure -platform linux-g++ -prefix /opt/trinity-host \
          ${CONFIGURE_COMMON} ${HOST_PROBE_FLAGS}
      make src-qmake src-moc sub-src
      cd tools/designer/uic && make )
fi
reloc_tquic "${HOSTTREE}/bin"

# ---------------------------------------------------------------------------
# Phase 2: cross build of the library + plugins.
# ---------------------------------------------------------------------------
echo "==> cross configure (host=linux-g++, target=substrate-g++)"
cd "${TREE}"
export TQTDIR="${TREE}"
export PKG_CONFIG_PATH=""   # never probe host pkg-config for X
yes yes | ./configure -platform linux-g++ -xplatform substrate-g++ ${CONFIGURE_COMMON}

# Drop the runnable host tquic over the (would-be cross) one.
reloc_tquic "${TREE}/bin"

echo "==> make -j${JOBS} (library + plugins only)"
make -j"${JOBS}" src-qmake src-moc sub-src sub-plugins

# ---------------------------------------------------------------------------
# Phase 3: stage into ${DESTDIR}/opt/trinity.
# ---------------------------------------------------------------------------
echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
( cd src         && make install INSTALL_ROOT="${DESTDIR}" )
( cd plugins/src && make install INSTALL_ROOT="${DESTDIR}" )

# Downstream qmake (tqtinterface/tdelibs) needs the mkspecs tree, in
# particular substrate-g++, found under $QTDIR/mkspecs.
mkdir -p "${DESTDIR}/opt/trinity"
cp -a "${TREE}/mkspecs" "${DESTDIR}/opt/trinity/mkspecs"

# Host build tools belong in /opt/trinity/bin as the uic/moc/qmake TDE uses.
mkdir -p "${DESTDIR}/opt/trinity/bin"
reloc_tquic "${DESTDIR}/opt/trinity/bin"
cp "${TREE}/bin/tqmoc"    "${DESTDIR}/opt/trinity/bin/tqmoc"
cp "${TREE}/qmake/tqmake" "${DESTDIR}/opt/trinity/bin/tqmake"

# Drop libtool archives and stamp ELFOSABI_SUBSTRATE on every cross .so.
find "${DESTDIR}" -name '*.la' -delete
_n=0
for so in $(find "${DESTDIR}/opt/trinity" -name '*.so*' -type f); do
    case "${so}" in */bin/*) continue ;; esac   # skip the host tquic helper lib
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none 2>/dev/null && _n=$((_n+1))
done
echo "  OSABI->substrate on ${_n} shared objects"
echo "==> Done.  TQt3 staged under ${DESTDIR}/opt/trinity"
