#!/bin/sh
#
# contrib/xset/build.sh — cross-build xset for substrate.
# xset is the X user-preferences utility: keyboard auto-repeat and bell,
# pointer acceleration, screen-saver and DPMS timeouts, font path, LED
# and bell settings.  Produces /usr/bin/xset.
#
# Depends on contrib/{xorgproto,libX11,libXext,libXmu} (xmuu = libXmuu).
# Optional xf86misc / fontcache support is disabled: substrate has
# neither the legacy XFree86-Misc nor the (removed) XFontCache extension.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.2.5"
TREE_DIR="${HERE}/build/xset-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-xset}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# The tarball's config.sub predates substrate and rejects the triple:
#     Invalid configuration `i386-unknown-substrate': OS `substrate' not recognized
# It works on a developer box only because the extracted tree there was
# patched by an earlier run.  substrate_config_sub_fix prefers the copy in
# the binutils port, patches the tree's own when that is absent (which is
# every CI run that hits the toolchain cache), and asserts the result.
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"

PKGP=""; CPP=""; LDF=""
for d in xorgproto xcb-proto libXau xtrans libxcb libX11 libXext libXmu; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib -Wl,-rpath-link,${st}/usr/lib"
done
PKGP="${PKGP}:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"

export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF} -Wl,--copy-dt-needed-entries"

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --without-fontcache \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  xset staged under ${DESTDIR}"
