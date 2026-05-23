#!/bin/sh
#
# contrib/pixman/fetch.sh — fetch the pixman tarball, verify,
# extract, apply the substrate patch series.
#
# pixman is the pixel-region / compositing helper library that
# every X server and many client libraries (cairo, etc.) rely on.
# Required dependency of xorg-server's kdrive backend (Xfbdev).

set -eu

VERSION="0.42.2"
TARBALL="pixman-${VERSION}.tar.gz"
URL="https://www.cairographics.org/releases/${TARBALL}"
SHA256="ea1480efada2fd948bc75366f7c349e1c96d3297d09a3fe62626e38e234a625e"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/pixman-${VERSION}"

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

if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        case "$p" in
            ''|'#'*) continue ;;
        esac
        if [ ! -f "${HERE}/patches/${p}" ]; then
            echo "fetch.sh: missing patch ${p}" >&2
            exit 1
        fi
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> Patch ${p} already applied"
            continue
        fi
        echo "==> Applying ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

# Libtool dispatch in `configure` doesn't know about substrate, so even
# with --enable-shared the build would emit only static .a files.  Add
# substrate to every linux-class case via in-place sed instead of
# carrying a brittle line-anchored patch (the line offsets vary
# between libtool versions; the substitution targets are stable).
cd "${TREE_DIR}"
if ! grep -q 'linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\* | substrate\*' configure; then
    echo "==> Adding substrate* to configure's libtool dispatch"
    sed -i \
        -e 's/linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\*/linux* | k*bsd*-gnu | kopensolaris*-gnu | gnu* | substrate*/g' \
        -e 's/gnu\* | linux\* | tpf\* | k\*bsd\*-gnu | kopensolaris\*-gnu/gnu* | linux* | tpf* | k*bsd*-gnu | kopensolaris*-gnu | substrate*/g' \
        -e 's/^    linux\*)$/    linux* | substrate*)/' \
        configure
fi

echo "==> pixman ${VERSION} ready at ${TREE_DIR}"
