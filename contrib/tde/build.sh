#!/bin/sh
# contrib/tde/build.sh — build every TDE sub-port, in dependency order.
#
# See fetch.sh for why this pair exists.  The chain, from README.SUBSTRATE.md:
#
#   tqt3          the Qt 3 fork; everything above depends on it.  Also
#                 produces the host tqmoc/tquic the CMake layers run.
#   tqtinterface  TQt <-> Qt compatibility shim (CMake)
#   dbus-1-tqt    TQt D-Bus binding, a tdelibs dependency
#   tdelibs       core libraries (tdecore, tdeui, tdeio, dcop)
#   tdebase       the desktop proper (twin, kicker, tdeinit)
#   tdeutils      \
#   tdegames       > optional application sets
#   tdetoys       /
#
# merge-staging.sh assembles dist-tde-sysroot and the pkg-config build-root
# that every CMake layer compiles against.  Its own header says it is
# "idempotent: safe to run before every sub-port, which is what the build
# scripts do" -- but only dbus-1-tqt actually called it, so each layer saw
# whatever the previous one happened to leave behind.  Run it here between
# layers, which is what makes the order above reproducible from clean.
#
# tde-cmake has no build.sh: it is CMake modules, consumed in place.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"

TDE_BUILD="tqt3 tqtinterface dbus-1-tqt tdelibs tdebase tdeutils tdegames tdetoys"

for _p in ${TDE_BUILD}; do
    [ -x "${HERE}/${_p}/build.sh" ] || {
        echo "build.sh: ${_p}/build.sh missing or not executable" >&2
        exit 1
    }
    # Refresh the merged sysroot so this layer sees everything below it.
    # tqt3 is the bottom of the stack and has nothing to merge yet.
    if [ "${_p}" != tqt3 ]; then
        "${HERE}/merge-staging.sh"
    fi
    echo "==> tde/${_p}: build"
    ( cd "${HERE}/${_p}" && ./build.sh )
done

# Final merge so dist-tde-sysroot reflects the completed stack.
"${HERE}/merge-staging.sh"

# Drop TQt3's HOST build tools (tqmoc, tquic, tqmake, the host libtqt-mt)
# from the staging tree, now that the last layer that runs them is built:
# they are x86-64 Linux binaries and would otherwise be overlaid onto the
# image.  See drop-host-tools.sh.
"${HERE}/drop-host-tools.sh"

# Strip the sub-port staging trees.
#
# build.sh's contrib loop strips each port's output, but it looks for
# dist-overlay/dist-<pkg> and this port is called "tde" -- there is no
# dist-tde.  Every layer stages under its OWN name (dist-tqt3, dist-tdelibs,
# ...), so the hook found nothing and silently skipped the single largest
# thing in the image: ~277 MB of -g -O2 objects, all of it DWARF that
# nothing on the target reads.  Strip them here, where the sub-port names
# are known.
_top="${SUBSTRATE_TOP:-$(cd "${HERE}/../.." && pwd)}"
for _sub in ${TDE_BUILD}; do
    _d="${_top}/dist-overlay/dist-${_sub}"
    if [ -d "${_d}" ]; then
        "${_top}/contrib/strip-staging.sh" "${_d}" "tde/${_sub}"
    fi
done

echo "==> TDE built; staged under dist-overlay/dist-{tqt3,tqtinterface,tdelibs,tdebase,...}"
