#!/bin/sh
#
# contrib/tde/merge-staging.sh — assemble the merged TDE sysroot and the
# unified pkg-config build-root that the CMake sub-ports build against.
#
# Why this exists
# ---------------
# Each sub-port stages into its own DESTDIR (dist-overlay/dist-<pkg>), but the
# ones further up the stack need to compile and link against everything below
# them through a SINGLE prefix.  Two views are needed:
#
#   dist-tde-sysroot/opt/trinity   the merged install tree.  tdelibs and up
#                                  take -DQT_PREFIX_DIR, tqmoc and tquic from
#                                  here, and the toolchain file puts its lib/
#                                  on -rpath-link.
#   tde-buildroot                  a symlink farm giving pkg-config one
#                                  PKG_CONFIG_SYSROOT_DIR that resolves both
#                                  the /usr-prefixed cross-sysroot .pc files
#                                  and the /opt/trinity-prefixed TQt/TDE ones.
#
# Until now nothing built the first one from tqt3/tqtinterface: those two stage
# only into their own trees, and tdelibs was the sole writer of the merged
# sysroot -- while also being a *reader* of it for tqmoc/tquic, which tqt3
# produces.  So a from-scratch build could not work; the merged tree was
# accumulated state left over from earlier runs, and wiping it (or building on
# a fresh checkout) broke the stack in confusing ways -- dbus-1-tqt, which
# assumes the build-root already exists, failed configure with "dbus-1 is
# required, but was not found" even though dbus was correctly installed.
#
# Idempotent: safe to run before every sub-port, which is what the build
# scripts do.  Later trees win, matching the overlay order build-rootfs.sh
# uses when it composes the image.
#
# Env: SUBSTRATE_TOP (default: three levels up), STAGE1_PREFIX (/opt/substrate)
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
: "${SUBSTRATE_TOP:=$(cd "${HERE}/../.." && pwd)}"
: "${STAGE1_PREFIX:=/opt/substrate}"

SR="${STAGE1_PREFIX}/i386-unknown-substrate"
OVERLAY="${SUBSTRATE_TOP}/dist-overlay"
MERGED="${OVERLAY}/dist-tde-sysroot"
TDEROOT="${OVERLAY}/tde-buildroot"

# Bottom-up: each later tree overlays the earlier ones.
ORDER="dist-tqt3 dist-tqtinterface dist-dbus-1-tqt dist-tdelibs dist-tdebase \
       dist-tdeutils dist-tdegames dist-tdetoys"

mkdir -p "${MERGED}/opt/trinity"

for pkg in ${ORDER}; do
    src="${OVERLAY}/${pkg}/opt/trinity"
    [ -d "${src}" ] || continue
    ( cd "${src}" && tar -cf - . ) | ( cd "${MERGED}/opt/trinity" && tar -xf - )
    echo "    merged ${pkg}"
done

# tdelibs exports an absolute-path CMake file; rewrite the baked-in /opt/trinity
# so consumers find the staged copy rather than a path on the target.
_export="${MERGED}/opt/trinity/share/cmake/tdelibs.cmake"
if [ -f "${_export}" ]; then
    sed -i "s|\"/opt/trinity/|\"${MERGED}/opt/trinity/|g" "${_export}"
fi

# Normalize the staged .pc files.
#
# A sub-port is configured with QT_PREFIX_DIR (and friends) pointing at the
# *host* staging directory, because that is where the headers and libraries
# actually are while cross-building.  Those absolute host paths get baked into
# the .pc files it installs -- tqtinterface's tqt.pc and tqtqui.pc carry
#   -I<overlay>/dist-tqt3/opt/trinity/include
# and tqt-mt.pc carries -L<cross sysroot>/lib.
#
# Consumers then read those .pc files with PKG_CONFIG_SYSROOT_DIR set to the
# build-root, and pkg-config prefixes the sysroot onto every -I/-L it emits.
# An already-absolute host path comes back out doubled:
#   -I<buildroot>/home/segin/.../dist-tqt3/opt/trinity/include
# which does not exist, so the consumer's configure fails with something
# entirely unrelated-sounding ("Unable to build a simple tqt test", because
# ntqglobal.h is not found).
#
# Rewrite both flavours to the paths the build-root actually presents:
# /opt/trinity for the merged TDE tree, /usr for the cross sysroot.  pkg-config
# then prefixes the sysroot exactly once and lands on the symlink farm.
for pc in "${MERGED}"/opt/trinity/lib/pkgconfig/*.pc; do
    [ -f "${pc}" ] || continue
    sed -i \
        -e "s|${OVERLAY}/dist-[a-zA-Z0-9._-]*/opt/trinity|/opt/trinity|g" \
        -e "s|${MERGED}/opt/trinity|/opt/trinity|g" \
        -e "s|${SR}|/usr|g" \
        "${pc}"
done

# The X .pc Requires chain (xrender -> x11 -> xcb -> pthread-stubs) needs the
# pthread-stubs stub .pc in the cross sysroot.
cp -f "${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig/"*.pc "${SR}/lib/pkgconfig/" \
    2>/dev/null || true

# Rebuild the symlink farm from scratch -- it is only symlinks, so this is
# cheap, and a stale link into a wiped tree is worse than no link.
rm -rf "${TDEROOT}"
mkdir -p "${TDEROOT}/opt"
ln -s "${SR}" "${TDEROOT}/usr"
ln -s "${MERGED}/opt/trinity" "${TDEROOT}/opt/trinity"

echo "==> merged sysroot: ${MERGED}/opt/trinity"
echo "==> build-root:     ${TDEROOT}"
