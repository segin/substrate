#!/bin/sh
# contrib/xorg-server/fetch.sh

set -eu

VERSION="1.16.4"
TARBALL="xorg-server-${VERSION}.tar.bz2"
URL="https://www.x.org/releases/individual/xserver/${TARBALL}"
SHA256="abb6e1cc9213a9915a121f48576ff6739a0b8cdb3d32796f9a7743c9a6efc871"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/xorg-server-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}"
    else
        wget -O "${TARBALL}" "${URL}"
    fi
fi

echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

# Add substrate to config.sub OS list (handle both old single-line and
# newer one-OS-per-line layouts).  No patch file — line offsets vary
# too much between config.sub versions to keep one patch portable.
cd "${TREE_DIR}"
if ! grep -q 'substrate\*' config.sub 2>/dev/null; then
    echo "==> Adding substrate* to config.sub OS allowlist"
    sed -i \
        -e 's/aos\* | aros\* | cloudabi\* | sortix\* | twizzler\*/aos* | aros* | cloudabi* | sortix* | substrate* | twizzler*/g' \
        -e 's/-aos\* | -aros\* \\/-aos* | -aros* | -substrate* \\/g' \
        -e '/^	| sortix\* \\$/a\	| substrate* \\' \
        config.sub
fi

# Add substrate to configure libtool dispatch.
if ! grep -q 'substrate\*' configure 2>/dev/null; then
    echo "==> Adding substrate* to configure libtool dispatch"
    sed -i \
        -e 's/linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\*/linux* | k*bsd*-gnu | kopensolaris*-gnu | gnu* | substrate*/g' \
        -e 's/gnu\* | linux\* | tpf\* | k\*bsd\*-gnu | kopensolaris\*-gnu/gnu* | linux* | tpf* | k*bsd*-gnu | kopensolaris*-gnu | substrate*/g' \
        -e 's/^    linux\*)$/    linux* | substrate*)/' \
        configure
fi

# Make kdrive's OS bring-up (hw/kdrive/linux/) compile for substrate
# too — substrate's evdev surface (/dev/input/event0 + Linux-style
# input_event struct) is close enough that the linux kdrive backend
# works as-is.  Without this, KDRIVELINUX stays no and the server
# link fails with undefined OsVendorInit / KdOsAddInputDrivers etc.
if ! grep -q '\*linux\* | \*substrate\*' configure 2>/dev/null; then
    echo "==> Enabling kdrive Linux input backend for substrate*"
    sed -i 's,	\*linux\*)$,	*linux* | *substrate*),' configure
fi

# Force KDRIVE_EVDEV / KDRIVE_KBD / KDRIVE_MOUSE to "yes" before the
# AC_DEFINE check fires.  Upstream configure.ac has the
# AC_DEFINE(KDRIVE_EVDEV) test BEFORE the host_os case that flips the
# default from "auto" to "yes" for linux/substrate — so with no
# explicit --enable flag the variables are still "auto" at define
# time and the macros never make it into kdrive-config.h, leaving
# `#ifdef KDRIVE_EVDEV` in linux.c false and the evdev / kbd / mouse
# input drivers unregistered.  X then prints "Couldn't find pointer
# driver evdev" at startup.  Inject a substrate-aware fallthrough at
# the top of the `if test "x$KDRIVE_KBD" = xyes` block.
if ! grep -q 'substrate kdrive defaults' configure 2>/dev/null; then
    echo "==> Forcing KDRIVE_EVDEV/KBD/MOUSE=yes for substrate"
    sed -i '/if test "x$KDRIVE_KBD" = xyes; then/i\
\
    # substrate kdrive defaults: upstream auto-detect runs too late.\
    case $host_os in *substrate*)\
        [ "x$KDRIVE_EVDEV" = xauto ] && KDRIVE_EVDEV=yes\
        [ "x$KDRIVE_KBD"   = xauto ] && KDRIVE_KBD=yes\
        [ "x$KDRIVE_MOUSE" = xauto ] && KDRIVE_MOUSE=yes\
        ;;\
    esac\
' configure
fi

# Apply substrate patch series.  Idempotent — patch's --dry-run -N
# short-circuits if the hunk is already in place.
if [ -d "${HERE}/patches" ]; then
    for p in "${HERE}/patches"/*.patch; do
        [ -f "${p}" ] || continue
        if patch -p1 -N --dry-run --silent <"${p}" >/dev/null 2>&1; then
            echo "==> Applying $(basename "${p}")"
            patch -p1 -N --silent <"${p}"
        else
            echo "==> Skipping $(basename "${p}") (already applied)"
        fi
    done
fi

echo "==> xorg-server ${VERSION} ready at ${TREE_DIR}"
