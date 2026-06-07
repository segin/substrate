#!/bin/sh
#
# contrib/cde/hosttools/build.sh — build, from source, the BUILD-HOST programs
# CDE's configure requires (rpcgen, ksh, compress, sessreg, mkfontdir,
# bdftopcf, onsgmls) into a self-contained prefix.  These run on the Linux
# build host during the CDE cross-build; none are installed on substrate.
#
# CDE's build.sh prepends ${PREFIX}/bin to PATH so configure finds them.
# Reproducible: every tool is fetched (SHA-256 verified), built and installed;
# anything already present in the prefix is skipped.  The X apps build against
# the host's system X protos (xproto/fontsproto/fontenc via pkg-config) plus
# util-macros built here.
#
# Env:  JOBS  parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${HERE}/build"
PREFIX="${HERE}/prefix"
SUBSTRATE_TOP="$(cd "${HERE}/../../.." && pwd)"
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

# --- util-macros: XORG_* m4 macros, build-time dep of the X apps ------------
if [ ! -f "${PREFIX}/share/aclocal/xorg-macros.m4" ]; then
    get util-macros-1.20.2.tar.xz \
        https://www.x.org/releases/individual/util/util-macros-1.20.2.tar.xz \
        9ac269eba24f672d7d7b3574e4be5f333d13f04a7712303b1821b2a51ac82e8e
    ( cd "${WORK}/util-macros-1.20.2" && ./configure --prefix="${PREFIX}" >/dev/null && make install >/dev/null )
    echo "==> util-macros ready"
fi

# --- ksh: build mksh for the host from the contrib/mksh source --------------
if ! have ksh; then
    MK="${SUBSTRATE_TOP}/contrib/mksh/build/mksh"
    [ -d "${MK}" ] || ( cd "${SUBSTRATE_TOP}/contrib/mksh" && ./fetch.sh )
    ( cd "${MK}" && env -u CC -u CFLAGS -u LDFLAGS CC=cc TARGET_OS=Linux sh Build.sh -Q >/dev/null 2>&1 )
    cp "${MK}/mksh" "${PREFIX}/bin/mksh"; ln -sf mksh "${PREFIX}/bin/ksh"
    echo "==> ksh (host mksh) ready"
fi

# --- rpcgen (rpcsvc-proto): ToolTalk RPC stub generator ---------------------
if ! have rpcgen; then
    get rpcsvc-proto-1.4.4.tar.xz \
        https://github.com/thkukuk/rpcsvc-proto/releases/download/v1.4.4/rpcsvc-proto-1.4.4.tar.xz \
        81c3aa27edb5d8a18ef027081ebb984234d5b5860c65bd99d4ac8f03145a558b
    ( cd "${WORK}/rpcsvc-proto-1.4.4" && ./configure --prefix="${PREFIX}" >/dev/null && \
        make -j"${JOBS}" >/dev/null && make install >/dev/null )
    echo "==> rpcgen ready"
fi

# --- compress/uncompress (ncompress) ----------------------------------------
if ! have compress; then
    get ncompress-5.0.tar.gz \
        https://github.com/vapier/ncompress/archive/refs/tags/v5.0.tar.gz \
        96ec931d06ab827fccad377839bfb91955274568392ddecf809e443443aead46
    # K&R definitions: GCC 15+/C23 rejects them, build as gnu89.
    ( cd "${WORK}/ncompress-5.0" && make CC="cc -std=gnu89" -j"${JOBS}" >/dev/null )
    cp "${WORK}/ncompress-5.0/compress" "${PREFIX}/bin/compress"
    ln -sf compress "${PREFIX}/bin/uncompress"
    echo "==> compress ready"
fi

# --- sessreg + mkfontscale (provides mkfontdir) + bdftopcf ------------------
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

# --- onsgmls (OpenSP): SGML parser used by dthelp --------------------------
if ! have onsgmls; then
    get OpenSP-1.5.2.tar.gz \
        https://downloads.sourceforge.net/project/openjade/opensp/1.5.2/OpenSP-1.5.2.tar.gz \
        57f4898498a368918b0d49c826aa434bb5b703d2c3b169beb348016ab25617ce
    # ~2005 C++; demote what GCC 16 would reject.
    ( cd "${WORK}/OpenSP-1.5.2" && ./configure --prefix="${PREFIX}" \
        --disable-nls --disable-doc-build --disable-shared \
        CXXFLAGS="-std=gnu++98 -fpermissive -O2 -w" CFLAGS="-std=gnu89 -O2 -w" >/dev/null && \
        make -j"${JOBS}" >/dev/null && make install >/dev/null )
    echo "==> onsgmls (OpenSP) ready"
fi

echo "==> host tools ready in ${PREFIX}/bin:"
echo "    $(cd "${PREFIX}/bin" && ls | tr '\n' ' ')"
