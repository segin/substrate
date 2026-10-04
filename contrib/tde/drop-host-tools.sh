#!/bin/sh
#
# contrib/tde/drop-host-tools.sh — drop TQt3's HOST build tools from the
# staging tree.
#
# tqt3 stages tqmoc, tquic, tqmake and the host libtqt-mt into
# opt/trinity/bin because the CMake layers above run them -- tdelibs takes
# tqmoc and tquic from QT_PREFIX_DIR.  They are x86-64 binaries, so once the
# last layer is built they are of no further use, and build-rootfs.sh
# overlays dist-overlay/dist-* wholesale: left in place they put ~144 MB of
# unrunnable host code into an i386 image, where /opt/trinity/bin/tqmoc
# would simply fail to exec.
#
# Detected by ELF class rather than by name, so a change to the tool set
# does not quietly start shipping again.  The TARGET libtqt-mt lives in
# opt/trinity/lib and is untouched, and so is the merged build sysroot
# (dist-tde-sysroot), which keeps its copies for the next build and is
# never overlaid onto an image.
#
# contrib/tde/build.sh runs this after the last layer.  It is a script of
# its own so that a staging tree left by a build that stopped early, or by
# one older than this step, can be cleaned without a two-hour rebuild:
#
#     contrib/tde/drop-host-tools.sh
#
# Env: SUBSTRATE_TOP (default: two levels up)
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
: "${SUBSTRATE_TOP:=$(cd "${HERE}/../.." && pwd)}"

_hostbin="${SUBSTRATE_TOP}/dist-overlay/dist-tqt3/opt/trinity/bin"
[ -d "${_hostbin}" ] || exit 0

_n=0
for _f in "${_hostbin}"/*; do
    [ -f "${_f}" ] || continue
    if [ -L "${_f}" ]; then continue; fi
    # ELF class 2 == 64-bit == built for the build host.
    if [ "$(od -An -tu1 -j4 -N1 "${_f}" 2>/dev/null | tr -d ' ')" = "2" ]; then
        rm -f "${_f}"; _n=$((_n + 1))
    fi
done
# Sweep the symlinks the removed files leave dangling.  Spelled as an
# if rather than a && chain: a chain that short-circuits is the last
# command in the loop body, and this script runs under set -e at the
# end of a two-hour build -- not the place to depend on which shell
# honours the && exemption.
for _f in "${_hostbin}"/*; do
    if [ -L "${_f}" ] && [ ! -e "${_f}" ]; then
        rm -f "${_f}"
    fi
done
echo "==> removed ${_n} host build tool(s) from dist-tqt3/opt/trinity/bin"
