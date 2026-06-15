#!/bin/sh
# scripts/install-xfbdev.sh — rebuild Xfbdev and push it everywhere
# the image-build pipeline might pick it up.  Stops the binary-drift
# bug where rebuilding the image regenerates from a stale dist/
# copy and reverts the deployed Xfbdev.
#
# Usage:
#   ./scripts/install-xfbdev.sh                # build + deploy
#   ./scripts/install-xfbdev.sh --no-build     # just deploy current build
#   ./scripts/install-xfbdev.sh --verify       # just md5-verify drift state

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
TOP="${HERE}/.."
cd "${TOP}"

BUILD_DIR="contrib/xorg-server/build/build-stage-substrate/hw/kdrive/fbdev"
BUILD_BIN="${BUILD_DIR}/Xfbdev"
DIST_BINS="
dist/usr/bin/Xfbdev
dist-xorg-server/usr/bin/Xfbdev
"
IMAGE="rootfs.img"
IMAGE_PATH="/usr/bin/Xfbdev"

PATH="/opt/substrate/bin:${PATH}"
export PATH

DO_BUILD=1
DO_VERIFY=0
for arg in "$@"; do
    case "$arg" in
        --no-build) DO_BUILD=0 ;;
        --verify)   DO_BUILD=0; DO_VERIFY=1 ;;
        -h|--help)
            sed -n '/^#/p' "$0" | head -10 | sed 's/^# //'
            exit 0
            ;;
        *)
            echo "install-xfbdev.sh: unknown arg: $arg" >&2
            exit 2
            ;;
    esac
done

if [ "${DO_BUILD}" -eq 1 ]; then
    echo "==> rebuilding Xfbdev"
    # Touch the source so make picks up any pending changes.
    touch contrib/xorg-server/build/xorg-server-1.16.4/fb/fbblt.c \
          contrib/xorg-server/build/xorg-server-1.16.4/fb/fbcopy.c \
          contrib/xorg-server/build/xorg-server-1.16.4/fb/fbfill.c \
          contrib/xorg-server/build/xorg-server-1.16.4/hw/kdrive/linux/evdev.c \
          contrib/xorg-server/build/xorg-server-1.16.4/hw/kdrive/src/kinput.c \
          contrib/xorg-server/build/xorg-server-1.16.4/dix/atom.c \
          contrib/xorg-server/build/xorg-server-1.16.4/os/utils.c \
          2>/dev/null || true

    ( cd contrib/xorg-server/build/build-stage-substrate/fb &&
      make 2>&1 | tail -3 )
    ( cd contrib/xorg-server/build/build-stage-substrate/dix &&
      make 2>&1 | tail -3 )
    ( cd contrib/xorg-server/build/build-stage-substrate/hw/kdrive/linux &&
      make 2>&1 | tail -3 )
    ( cd contrib/xorg-server/build/build-stage-substrate/hw/kdrive/src &&
      make 2>&1 | tail -3 )
    ( cd contrib/xorg-server/build/build-stage-substrate/os &&
      make 2>&1 | tail -3 )
    ( cd "${BUILD_DIR}" && rm -f Xfbdev && make Xfbdev 2>&1 | tail -3 )
fi

if [ ! -f "${BUILD_BIN}" ]; then
    echo "==> ERROR: build artifact missing: ${BUILD_BIN}" >&2
    exit 1
fi

BUILD_MD5=$(md5sum "${BUILD_BIN}" | awk '{print $1}')
echo "==> build:  ${BUILD_MD5}  ${BUILD_BIN}"

# Copy to every dist/ location.
for dst in ${DIST_BINS}; do
    if [ -d "$(dirname "${dst}")" ]; then
        if [ "${DO_VERIFY}" -eq 0 ]; then
            cp "${BUILD_BIN}" "${dst}"
        fi
        dst_md5=$(md5sum "${dst}" 2>/dev/null | awk '{print $1}')
        marker=$( [ "${dst_md5}" = "${BUILD_MD5}" ] && echo OK || echo DRIFT )
        echo "==> ${marker}:    ${dst_md5}  ${dst}"
    fi
done

# Push into the rootfs image via debugfs.
if [ -f "${IMAGE}" ]; then
    if [ "${DO_VERIFY}" -eq 0 ]; then
        debugfs -w -R "rm ${IMAGE_PATH}" "${IMAGE}" >/dev/null 2>&1 || true
        debugfs -w -R "write ${BUILD_BIN} ${IMAGE_PATH}" "${IMAGE}" >/dev/null 2>&1
        debugfs -w -R "set_inode_field ${IMAGE_PATH} mode 0100755" "${IMAGE}" >/dev/null 2>&1
    fi
    tmp_extract=$(mktemp /tmp/xfbdev_in_img.XXXXXX)
    debugfs -R "dump ${IMAGE_PATH} ${tmp_extract}" "${IMAGE}" >/dev/null 2>&1
    img_md5=$(md5sum "${tmp_extract}" 2>/dev/null | awk '{print $1}')
    rm -f "${tmp_extract}"
    marker=$( [ "${img_md5}" = "${BUILD_MD5}" ] && echo OK || echo DRIFT )
    echo "==> ${marker}:    ${img_md5}  ${IMAGE}:${IMAGE_PATH}"
fi

echo "==> done"
