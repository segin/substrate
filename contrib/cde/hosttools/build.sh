#!/bin/sh
#
# contrib/cde/hosttools/build.sh — build, from source, every program that has
# to run on the BUILD HOST during CDE's cross-build, into a self-contained
# prefix.  None of these are installed on substrate; they exist only so the
# cross-build can complete.
#
# Three distinct kinds of tool live here:
#
#   1. Plain build dependencies CDE's configure looks for on the host:
#      rpcgen, ksh, compress, sessreg, mkfontdir, bdftopcf, onsgmls.
#
#   2. tradcpp — CDE's own traditional C preprocessor.  CDE builds it and
#      then RUNS it to expand its *.cpp config templates.  A cross-build
#      compiles it for the target, where it cannot execute, so it is built
#      here for the host instead.
#
#   3. Generators CDE builds and runs mid-build against its own libraries:
#      dtcodegen (AppBuilder UI generator), pmaker/dfiles/msgsets (dtinfo),
#      mkdbd (dtsr), mkcatdefs/merge (message catalogs), tt_type_comp
#      (ToolTalk type compiler), lineToData, mk_fonts_alias and the dthelp
#      SGML parser tools.  Several of these link CDE's own libDt*/libtt, so
#      host copies mean building a whole second, native objdir of the CDE
#      tree (cde-host/).  build.sh does not copy binaries out of it: it
#      points CDE's own generator variables at that tree, so each Makefile
#      picks up the native build of the generator it would have run.
#
# Everything is fetched with a SHA-256 check and skipped if already present.
#
# Env:  JOBS  parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${HERE}/build"
PREFIX="${HERE}/prefix"

SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
SRC="${HERE}/../build/cdesktopenv/cde"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

mkdir -p "${WORK}" "${PREFIX}/bin"

export PATH="${PREFIX}/bin:${PATH}"
export ACLOCAL_PATH="${PREFIX}/share/aclocal"
export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PREFIX}/share/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig"

have() { command -v "$1" >/dev/null 2>&1; }

# get <file> <url> <sha256> — fetch into WORK, verify, extract
get() {
    cd "${WORK}"
    [ -f "$1" ] || { echo "==> fetch $1"; curl -fSL -o "$1" "$2"; }
    echo "$3  $1" | sha256sum -c - >/dev/null
    case "$1" in *.tar.*) tar xf "$1" ;; esac
}

[ -d "${SRC}" ] || { echo "hosttools: CDE source tree missing — run cde/fetch.sh first" >&2; exit 1; }

# ===========================================================================
# 1. Host build dependencies
# ===========================================================================

# util-macros: the XORG_* m4 macros the X apps below need at autoconf time.
if [ ! -f "${PREFIX}/share/aclocal/xorg-macros.m4" ]; then
    get util-macros-1.20.2.tar.xz \
        https://www.x.org/releases/individual/util/util-macros-1.20.2.tar.xz \
        9ac269eba24f672d7d7b3574e4be5f333d13f04a7712303b1821b2a51ac82e8e
    ( cd "${WORK}/util-macros-1.20.2" && ./configure --prefix="${PREFIX}" >/dev/null && make install >/dev/null )
    echo "==> util-macros ready"
fi

# ksh: CDE's build scripts are ksh scripts.  Reuse contrib/mksh's source and
# build it for the host.
if ! have ksh; then
    MK="${SUBSTRATE_TOP}/contrib/mksh/build/mksh"
    [ -d "${MK}" ] || ( cd "${SUBSTRATE_TOP}/contrib/mksh" && ./fetch.sh )
    ( cd "${MK}" && env -u CC -u CFLAGS -u LDFLAGS CC=cc TARGET_OS=Linux sh Build.sh -Q >/dev/null 2>&1 )
    cp "${MK}/mksh" "${PREFIX}/bin/mksh"; ln -sf mksh "${PREFIX}/bin/ksh"
    echo "==> ksh (host mksh) ready"
fi

# rpcgen: ToolTalk's RPC stub generator.
if ! have rpcgen; then
    get rpcsvc-proto-1.4.4.tar.xz \
        https://github.com/thkukuk/rpcsvc-proto/releases/download/v1.4.4/rpcsvc-proto-1.4.4.tar.xz \
        81c3aa27edb5d8a18ef027081ebb984234d5b5860c65bd99d4ac8f03145a558b
    ( cd "${WORK}/rpcsvc-proto-1.4.4" && ./configure --prefix="${PREFIX}" >/dev/null && \
        make -j"${JOBS}" >/dev/null && make install >/dev/null )
    echo "==> rpcgen ready"
fi

# compress/uncompress: CDE compresses parts of its help/info data.
if ! have compress; then
    get ncompress-5.0.tar.gz \
        https://github.com/vapier/ncompress/archive/refs/tags/v5.0.tar.gz \
        96ec931d06ab827fccad377839bfb91955274568392ddecf809e443443aead46
    # K&R definitions throughout; GCC 15+ defaults to C23 and rejects them.
    ( cd "${WORK}/ncompress-5.0" && make CC="cc -std=gnu89" -j"${JOBS}" >/dev/null )
    cp "${WORK}/ncompress-5.0/compress" "${PREFIX}/bin/compress"
    ln -sf compress "${PREFIX}/bin/uncompress"
    echo "==> compress ready"
fi

# sessreg / mkfontdir / bdftopcf: X apps configure probes for.  They build
# against the host's system X protos via pkg-config.
if ! have sessreg; then
    get sessreg-1.1.3.tar.xz \
        https://www.x.org/releases/individual/app/sessreg-1.1.3.tar.xz \
        022acd5de8077dddc4f919961f79e102ecd5f3228a333681af5cd0e7344facc2
    ( cd "${WORK}/sessreg-1.1.3" && ./configure --prefix="${PREFIX}" >/dev/null && make install >/dev/null )
    echo "==> sessreg ready"
fi
if ! have mkfontdir; then
    get mkfontscale-1.2.3.tar.xz \
        https://www.x.org/releases/individual/app/mkfontscale-1.2.3.tar.xz \
        2921cdc344f1acee04bcd6ea1e29565c1308263006e134a9ee38cf9c9d6fe75e
    ( cd "${WORK}/mkfontscale-1.2.3" && ./configure --prefix="${PREFIX}" >/dev/null && \
        make -j"${JOBS}" >/dev/null && make install >/dev/null )
    echo "==> mkfontscale + mkfontdir ready"
fi
if ! have bdftopcf; then
    get bdftopcf-1.1.tar.gz \
        https://www.x.org/releases/individual/app/bdftopcf-1.1.tar.gz \
        699d1a62012035b1461c7f8e3f05a51c8bd6f28f348983249fb89bbff7309b47
    ( cd "${WORK}/bdftopcf-1.1" && ./configure --prefix="${PREFIX}" >/dev/null && \
        make -j"${JOBS}" >/dev/null && make install >/dev/null )
    echo "==> bdftopcf ready"
fi

# onsgmls (OpenSP): the SGML parser dthelp's document compiler drives.
if ! have onsgmls; then
    get OpenSP-1.5.2.tar.gz \
        https://downloads.sourceforge.net/project/openjade/opensp/1.5.2/OpenSP-1.5.2.tar.gz \
        57f4898498a368918b0d49c826aa434bb5b703d2c3b169beb348016ab25617ce
    # ~2005 C++; demote what GCC 16 would reject outright.
    ( cd "${WORK}/OpenSP-1.5.2" && ./configure --prefix="${PREFIX}" \
        --disable-nls --disable-doc-build --disable-shared \
        CXXFLAGS="-std=gnu++98 -fpermissive -O2 -w" CFLAGS="-std=gnu89 -O2 -w" >/dev/null && \
        make -j"${JOBS}" >/dev/null && make install >/dev/null )
    echo "==> onsgmls (OpenSP) ready"
fi

# ===========================================================================
# 2. tradcpp — CDE's own preprocessor, built for the host
# ===========================================================================
# config.h here is compile-time OS detection only, so the in-tree one is
# portable and no separate configure run is needed.
if ! have tradcpp; then
    ( cd "${SRC}/util/tradcpp" && cc -O2 -w -I. -o "${PREFIX}/bin/tradcpp" \
        array.c directive.c eval.c files.c macro.c main.c output.c place.c utils.c )
    echo "==> tradcpp (host) ready"
fi

# ===========================================================================
# 3. The native CDE objdir
# ===========================================================================
# A pristine copy of the fetched tree, configured for and built on the build
# host.  Everything CDE builds and then RUNS during its own build lives in
# here as a runnable native binary; the cross build points its generator
# variables at this tree (CDE_HOST) instead of copying binaries around.
#
# The build is -k: the target-only parts and the DtMmdb C++ bits do not build
# natively, and none of them are generators.  What matters is that the
# generator binaries listed at the end exist afterwards.
HB="${HERE}/cde-host"
if [ ! -f "${HB}/.substrate-hostbuild-done" ]; then
    echo "==> native CDE objdir at ${HB} (this takes a while)"
    rm -rf "${HB}"
    cp -a "${SRC}" "${HB}"
    ( cd "${HB}" && make distclean >/dev/null 2>&1 || true )
    find "${HB}" -name '*.o' -delete
    # Keep only the -Wno-error= names this compiler actually has.
    #
    # An unknown one is not ignored -- gcc rejects it outright:
    #
    #   cc1: error: '-Wno-error=return-mismatch': no option '-Wreturn-mismatch'
    #
    # and since these are in CPPFLAGS that kills every conftest compile, so
    # configure reports the uninformative "C compiler cannot create
    # executables".  -Wreturn-mismatch arrived in GCC 14: this list was
    # written against a GCC 16 host and fails wholesale on the GCC 13 that
    # Ubuntu 24.04 ships.  Probe instead of assuming.
    _hostwarn=""
    for _w in incompatible-pointer-types int-conversion \
              implicit-function-declaration return-mismatch format; do
        if echo 'int main(void){return 0;}' | \
           gcc -Wno-error="${_w}" -x c -c - -o /dev/null 2>/dev/null; then
            _hostwarn="${_hostwarn} -Wno-error=${_w}"
        fi
    done

    # On failure, show the part of config.log that says WHY.  autoconf's
    # "C compiler cannot create executables" names nothing, the runner is
    # gone by the time anyone looks, and this tree is deep enough that the
    # workflow's own config.log dump does not glob it.
    if ! ( cd "${HB}" && ./configure \
        --prefix=/nonexistent \
        CC=gcc CXX=g++ \
        CPPFLAGS="-I/usr/include/tirpc${_hostwarn}" \
        LIBS="-ltirpc" \
        --with-tcl=/usr/lib >/dev/null ); then
        echo "hosttools: native CDE objdir: configure failed" >&2
        if [ -f "${HB}/config.log" ]; then
            # The TAIL is where autoconf records what actually stopped it --
            # the first-compiler-test region is boilerplate whenever the
            # compiler itself is fine, which cost a round trip to learn.
            echo "--- ${HB}/config.log (last 60 lines) ---" >&2
            tail -60 "${HB}/config.log" >&2
            echo "--- configure: error lines ---" >&2
            grep -E "^configure: error|: error:" "${HB}/config.log" >&2 || true
        fi
        exit 1
    fi
    ( cd "${HB}" && make -C util -j"${JOBS}" >/dev/null )
    # -k, and the output goes to a log rather than /dev/null: the target-only
    # parts and the DtMmdb C++ bits are EXPECTED to fail natively, so the
    # build cannot be fatal -- but when one of the libraries below is missing
    # afterwards, that log is the only record of why, and discarding it meant
    # the check reported "failed to build" and nothing else.
    _liblog="${HB}/.substrate-native-lib.log"
    ( cd "${HB}" && make -C lib -k -j"${JOBS}" >"${_liblog}" 2>&1 || true )
    for la in tt/lib/libtt.la DtSvc/libDtSvc.la DtWidget/libDtWidget.la \
              DtHelp/libDtHelp.la DtTerm/libDtTerm.la; do
        [ -f "${HB}/lib/${la}" ] && continue
        echo "hosttools: native CDE objdir: lib/${la} failed to build" >&2
        _d=$(dirname "${la}")
        echo "--- errors from ${_liblog} under lib/${_d} ---" >&2
        grep -E ": (error|fatal error):|No such file|Error [0-9]+$" "${_liblog}" \
            | grep -E "${_d}|Error [0-9]+$" | tail -25 >&2 || true
        echo "--- last 25 lines of the native lib build ---" >&2
        tail -25 "${_liblog}" >&2
        exit 1
    done
    # -static-libtool-libs: link CDE's own libtool libraries into each
    # generator so it runs standalone; the system X/Motif/tirpc libraries
    # stay dynamic.
    ( cd "${HB}" && make -C programs -k -j"${JOBS}" \
        LDFLAGS=-static-libtool-libs LIBS="-ltirpc -lstdc++" >/dev/null 2>&1 || true )
    touch "${HB}/.substrate-hostbuild-done"
fi

# Report which generators came out of it.  A missing one is not fatal here —
# build.sh decides what it can build from what is present — but it is the
# thing to look at when a cross-build stage turns out to be skipped.
echo "==> native generators:"
for g in lib/DtTerm/util/lineToData \
         programs/fontaliases/mk_fonts_alias \
         programs/localized/util/merge \
         programs/localized/util/mkcatdefs \
         lib/tt/bin/tt_type_comp/tt_type_comp \
         programs/dtdocbook/dtsr/mkdbd \
         programs/dtinfo/tools/misc/pmaker \
         programs/dtinfo/tools/misc/dfiles \
         programs/dtinfo/tools/misc/msgsets \
         programs/dtappbuilder/src/abmf/dtcodegen \
         programs/dthelp/parser/pass1/eltdef/eltdef \
         programs/dthelp/parser/pass1/build/build \
         programs/dthelp/parser/pass1/util/context; do
    if [ -x "${HB}/${g}" ]; then echo "    ok      ${g}"; else echo "    MISSING ${g}"; fi
done
if [ ! -f /usr/include/Xm/Xm.h ]; then
    echo "hosttools: NOTE: no host Motif (/usr/include/Xm/Xm.h) — dtcodegen cannot be" >&2
    echo "hosttools:       built here, so dtappbuilder and ttsnoop will be skipped." >&2
    echo "hosttools:       Install one (e.g. pacman -S openmotif) to enable them." >&2
fi

# ===========================================================================
# 4. crossexec — run substrate binaries during the build
# ===========================================================================
# ksh93's build system (AST package/mamake) compiles feature probes with iffe
# and then RUNS them, so its FEATURE headers describe whatever machine the
# probe executed on.  crossexec boots a private copy of the substrate root
# image headlessly in qemu, runs the probe there, and relays stdout and exit
# status back — so the headers describe substrate.  Needs a baked rootfs.img:
# on a fresh checkout, bake one and re-run this script to pick dtksh up.
install -m755 "${HERE}/crossexec-harness/crossexec" "${PREFIX}/bin/crossexec"
EXEC_IMG="${SUBSTRATE_EXEC_IMG:-${WORK}/sub-exec.img}"
if [ ! -f "${EXEC_IMG}" ] && [ -f "${SUBSTRATE_TOP}/rootfs.img" ]; then
    echo "==> crossexec: preparing exec image at ${EXEC_IMG}"
    cp --reflink=auto "${SUBSTRATE_TOP}/rootfs.img" "${EXEC_IMG}"
fi
if [ -f "${EXEC_IMG}" ]; then
    # rootfs.img is a partitioned GRUB disk; the ext2 root starts at this
    # offset, which debugfs takes as a suffix on the device name.
    EXT2="${EXEC_IMG}?offset=53477376"
    debugfs -w -R "rm /root/iffe-run.sh" "${EXT2}" >/dev/null 2>&1 || true
    debugfs -w -R "write ${HERE}/crossexec-harness/iffe-run.sh /root/iffe-run.sh" "${EXT2}" >/dev/null 2>&1
    debugfs -w -R "sif /root/iffe-run.sh mode 0100755" "${EXT2}" >/dev/null 2>&1
    debugfs -R "stat /root/iffe-run.sh" "${EXT2}" >/dev/null 2>&1 || {
        echo "hosttools: crossexec: could not inject /root/iffe-run.sh into ${EXEC_IMG}" >&2
        exit 1
    }
    echo "==> crossexec ready (exec image: ${EXEC_IMG})"
else
    echo "hosttools: NOTE: no rootfs.img yet — crossexec installed but unusable until an image is baked (dtksh will be skipped)" >&2
fi

echo "==> host tools ready in ${PREFIX}/bin:"
echo "    $(cd "${PREFIX}/bin" && ls | tr '\n' ' ')"
echo "==> native CDE objdir (generators): ${HB}"
