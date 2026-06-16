#!/bin/sh
# contrib/pkg-config/build.sh — cross-build pkg-config 0.29.2 for substrate.
#
# Built --with-internal-glib so it needs no external glib: the bundled glib 2.x
# subset is cross-compiled alongside.  --disable-host-tool suppresses the
# "${host}-pkg-config" convenience symlink (we only want /usr/bin/pkg-config).
# The default .pc search path is baked to substrate's lib dirs so a bare
# `pkg-config --cflags foo` on-target finds /usr/lib/pkgconfig and friends.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="0.29.2"
TREE_DIR="${HERE}/build/pkg-config-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-pkg-config}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
# Throwaway cache copy: configure rewrites env vars into it, and the
# env-consistency check trips if CFLAGS differ between runs.  Strip ac_cv_env_*.
CACHE="${BUILD_DIR}/config.cache"
grep -v '^ac_cv_env_' "${HERE}/substrate-cross.cache" > "${CACHE}"

# Where pkg-config looks for .pc files when PKG_CONFIG_PATH is unset on-target.
PC_PATH="/usr/lib/pkgconfig:/usr/share/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --with-internal-glib \
    --disable-host-tool \
    --with-pc-path="${PC_PATH}" \
    --cache-file="${CACHE}" \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -std=gnu11 -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=format -Wno-error=format-security"
echo "==> make -j${JOBS}"
make -j"${JOBS}"
echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
echo "==> Done.  Staged at ${DESTDIR}/usr/bin/pkg-config"
