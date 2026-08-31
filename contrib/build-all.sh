#!/bin/sh
#
# build-all.sh — fetch and build every contrib port.
#
# The order is DISCOVERED, not declared.  Each pass attempts every port
# that has not been built yet; the ones that succeed are retired and the
# next pass retries the rest.  When a pass builds nothing, whatever is
# left has failed for a reason other than a missing dependency, and the
# script reports those and exits non-zero.
#
# Why not a build-order manifest?  Because one cannot be derived from the
# ports, and the two obvious sources both lie:
#
#   - The `for d in ...` lists in build.sh look like dependency
#     declarations but are sysroot merge lists -- "copy in whatever is
#     staged".  libICE's list names libICE and libXaw; xterm's tolerates
#     13 of its 14 entries being absent.  Read as edges they put libICE,
#     libSM, libXt, libXaw, libXext, libXmu and libXpm in a mutual cycle.
#   - The prose in README.SUBSTRATE.md is worse: xorgproto's "Depends on"
#     sentence lists the packages that depend on IT.
#
# A hand-written manifest would work but goes stale silently -- a new port
# or a changed dependency leaves it wrong until someone notices a build
# failing for the wrong reason.  Repeated passes stay correct for free, at
# the cost of some wasted attempts in the early rounds.  ORDER_HINT below
# keeps that cost small by putting the base of the stack first; it is only
# a hint, and being wrong costs a retry, not a failure.
#
# Env:
#   SUBSTRATE_TOP   repo root (default: the directory above this script)
#   STAGE1_PREFIX   substrate cross toolchain (default /opt/substrate)
#   PORTS           space-separated subset to build (default: all)
#   SKIP            space-separated ports to skip (default: the toolchain,
#                   which contrib/build-toolchain.sh owns)
#   LOGDIR          per-port logs (default $SUBSTRATE_TOP/contrib/.build-logs)
#   MAX_PASSES      give up after this many passes (default 8)
#
# Usage:
#   ./build-all.sh                 # everything
#   ./build-all.sh --dry-run       # show the attempt order and exit
#   PORTS="zlib libiconv" ./build-all.sh
#

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
: "${SUBSTRATE_TOP:=$(cd "${HERE}/.." && pwd)}"
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${LOGDIR:=${SUBSTRATE_TOP}/contrib/.build-logs}"
: "${MAX_PASSES:=8}"

# binutils and gcc are the toolchain; contrib/build-toolchain.sh builds
# those in their own two stages and this script must not race it.
: "${SKIP:=binutils gcc}"

DRY_RUN=0
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        -h|--help) sed -n '2,/^$/p' "$0"; exit 0 ;;
        *) echo "build-all.sh: unknown argument: $arg" >&2; exit 2 ;;
    esac
done

# The base of the stack, roughly bottom-up.  Only a hint -- see the header.
ORDER_HINT="zlib libiconv bzip2 gzip expat libffi libpng libjpeg freetype
            ncurses pkg-config openssl sqlite3 tcl
            xorgproto xcb-proto libXau xtrans libxcb libX11
            libXext libICE libSM libXt libXmu libXpm libXaw
            libXrender libXfixes libXcursor libXi libXinerama libXScrnSaver
            fontconfig libXft libfontenc libXfont libXfont2 libxkbfile
            pixman cairo harfbuzz fribidi pango
            glib glib2 atk gdk-pixbuf gtk1 gtk2 motif"

available() {
    for d in "${HERE}"/*/; do
        p=${d%/}; p=${p##*/}
        [ -f "${d}build.sh" ] || continue
        echo "$p"
    done
}

is_skipped() {
    for s in ${SKIP}; do [ "$1" = "$s" ] && return 0; done
    return 1
}

# Attempt order: hint first (when present and wanted), then the rest.
pending=""
all="$(available)"
want="${PORTS:-${all}}"
for h in ${ORDER_HINT}; do
    for p in ${want}; do
        [ "$p" = "$h" ] || continue
        is_skipped "$p" && continue
        case " ${pending} " in *" $p "*) ;; *) pending="${pending} $p" ;; esac
    done
done
for p in ${want}; do
    is_skipped "$p" && continue
    [ -f "${HERE}/${p}/build.sh" ] || { echo "build-all.sh: no such port: $p" >&2; exit 2; }
    case " ${pending} " in *" $p "*) ;; *) pending="${pending} $p" ;; esac
done

if [ "${DRY_RUN}" = 1 ]; then
    echo "attempt order ($(echo ${pending} | wc -w) ports):"
    for p in ${pending}; do echo "  $p"; done
    exit 0
fi

mkdir -p "${LOGDIR}"
export SUBSTRATE_TOP STAGE1_PREFIX
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

built=""
failed=""
pass=0

while [ -n "${pending}" ] && [ "${pass}" -lt "${MAX_PASSES}" ]; do
    pass=$((pass + 1))
    progress=0
    still=""
    n=$(echo ${pending} | wc -w)
    echo "=== pass ${pass}: ${n} port(s) pending ==="

    for p in ${pending}; do
        log="${LOGDIR}/${p}.log"
        printf '  %-22s ' "$p"
        {
            echo "### pass ${pass}: $(date -u +%FT%TZ)"
            echo "### fetch"
        } >> "${log}"
        if ! ( cd "${HERE}/${p}" && ./fetch.sh ) >> "${log}" 2>&1; then
            echo "FETCH FAILED  (${log})"
            still="${still} $p"
            continue
        fi
        echo "### build" >> "${log}"
        if ( cd "${HERE}/${p}" && ./build.sh ) >> "${log}" 2>&1; then
            echo "ok"
            built="${built} $p"
            progress=$((progress + 1))
        else
            echo "failed (retry next pass)"
            still="${still} $p"
        fi
    done

    pending="${still}"
    [ "${progress}" -eq 0 ] && break
done

echo
echo "=== summary ==="
echo "built:   $(echo ${built} | wc -w)"
echo "pending: $(echo ${pending} | wc -w)"
if [ -n "${pending}" ]; then
    echo
    echo "these did not build after ${pass} pass(es) -- the last pass made no"
    echo "progress, so they are not waiting on each other:"
    for p in ${pending}; do
        echo "  ${p}   (tail of ${LOGDIR}/${p}.log)"
        tail -n 12 "${LOGDIR}/${p}.log" 2>/dev/null | sed 's/^/      /'
    done
    exit 1
fi
echo "all ports built."
