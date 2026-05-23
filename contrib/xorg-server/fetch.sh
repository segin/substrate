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

echo "==> xorg-server ${VERSION} ready at ${TREE_DIR}"
