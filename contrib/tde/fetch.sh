#!/bin/sh
# contrib/tde/fetch.sh — fetch every TDE sub-port, in dependency order.
#
# TDE is the one port in contrib/ that is not a single upstream package: it is
# nine of them, layered.  build.sh's contrib loop drives contrib/<pkg>/fetch.sh
# and contrib/<pkg>/build.sh, so this pair exists to present that stack as one
# port and let TDE appear in DEFAULT_CONTRIB like anything else.
#
# The order is the dependency chain in README.SUBSTRATE.md, and it is shared
# with build.sh -- keep the two lists identical.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"

# tde-cmake is modules only: it has a fetch.sh and no build.sh.
TDE_FETCH="tde-cmake tqt3 tqtinterface dbus-1-tqt tdelibs tdebase tdeutils tdegames tdetoys"

for _p in ${TDE_FETCH}; do
    [ -d "${HERE}/${_p}" ] || { echo "fetch.sh: no such sub-port ${_p}" >&2; exit 1; }
    [ -x "${HERE}/${_p}/fetch.sh" ] || {
        echo "fetch.sh: ${_p}/fetch.sh missing or not executable" >&2
        exit 1
    }
    echo "==> tde/${_p}: fetch"
    ( cd "${HERE}/${_p}" && ./fetch.sh "$@" )
done

echo "==> TDE sources ready"
