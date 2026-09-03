#!/bin/sh
# contrib/libXfont/fetch.sh

set -eu

VERSION="1.5.4"
TARBALL="libXfont-${VERSION}.tar.bz2"
URL="https://www.x.org/releases/individual/lib/${TARBALL}"
SHA256="1a7f7490774c87f2052d146d1e0e64518d32e6848184a18654e8d0bb57883242"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/libXfont-${VERSION}"

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

# Always re-extract.  Keeping an existing tree made this port depend on
# whatever a previous run -- or a hand edit -- had left behind, and the
# patch series below was never applied by this script at all, so it lived
# only in trees old enough to predate it.  A clean checkout therefore built
# a libXfont without the builtin font aliases and nobody noticed.
echo "==> Extracting"
rm -rf "${TREE_DIR}"
tar xf "${TARBALL}"

echo "==> Applying patch series"
cd "${TREE_DIR}"
while IFS= read -r p || [ -n "${p}" ]; do
    case "${p}" in ''|\#*) continue;; esac
    echo "    - ${p}"
    patch -p1 -F0 < "${HERE}/patches/${p}"
done < "${HERE}/series"
cd "${BUILD_DIR}"

# Add substrate to config.sub OS list.  Three layouts in the wild: the
# 2015-era dashed list ("| -sortix* \"), which is what libXfont 1.5.4
# ships and which neither of the other two expressions matched; the
# undashed inline list; and the one-OS-per-line form.  No patch file --
# line offsets vary too much between config.sub versions.
cd "${TREE_DIR}"
if ! grep -q 'substrate\*' config.sub 2>/dev/null; then
    echo "==> Adding substrate* to config.sub OS allowlist"
    sed -i \
        -e 's/| -sortix\*/| -sortix* | -substrate*/' \
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

echo "==> libXfont ${VERSION} ready at ${TREE_DIR}"
