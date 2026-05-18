#!/bin/sh
#
# fetch.sh — apply substrate patch series to the in-tree expr.c.
#
# Unlike most contrib ports there's no upstream tarball to fetch:
# OpenBSD's bin/expr is a single .c file that lives in-tree at
# contrib/expr/expr.c (kept pristine).  The "build" tree just
# carries a patched working copy so build.sh can compile against
# it without touching the source-of-truth.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"

mkdir -p "${BUILD_DIR}"
cp "${HERE}/expr.c" "${BUILD_DIR}/expr.c"
cp "${HERE}/expr.1" "${BUILD_DIR}/expr.1"

if [ -f "${HERE}/series" ]; then
    cd "${BUILD_DIR}"
    while IFS= read -r p; do
        case "$p" in
            ''|'#'*) continue ;;
        esac
        if [ ! -f "${HERE}/patches/${p}" ]; then
            echo "fetch.sh: missing patch ${p}" >&2
            exit 1
        fi
        echo "    apply ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> Ready at ${BUILD_DIR}"
