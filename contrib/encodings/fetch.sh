#!/bin/sh
# contrib/encodings/fetch.sh

set -eu

VERSION="1.1.0"
TARBALL="encodings-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/font/${TARBALL}"
SHA256="9ff13c621756cfa12e95f32ba48a5b23839e8f577d0048beda66c67dab4de975"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/encodings-${VERSION}"

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

echo "==> encodings ${VERSION} ready at ${TREE_DIR}"
