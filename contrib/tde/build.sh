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
echo "==> TDE built; staged under dist-overlay/dist-{tqt3,tqtinterface,tdelibs,tdebase,...}"
