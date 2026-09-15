#!/bin/sh
# contrib/freetype-harfbuzz/build.sh -- rebuild FreeType with HarfBuzz.
#
# FreeType and HarfBuzz depend on each other, so FreeType is built twice.
# contrib/freetype is the first pass (--without-harfbuzz); harfbuzz is built
# against it; this port then reruns contrib/freetype/build.sh with
# FREETYPE_WITH_HARFBUZZ=1, replacing dist-overlay/dist-freetype, so the image
# and every later port get a FreeType that uses HarfBuzz.
#
# The staging tree it writes belongs to "freetype", not to this port's name,
# so build.sh's per-port strip and sysroot sync (which look for
# dist-freetype-harfbuzz) would find nothing.  Do both here for dist-freetype.
#
# The result has a DT_NEEDED cycle -- libfreetype.so.6 needs libharfbuzz.so.0,
# which needs libfreetype.so.6.  ld.so loads by SONAME with a loaded-object
# check before every load, so the cycle resolves to one copy of each.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
export SUBSTRATE_TOP
: "${STAGE1_PREFIX:=/opt/substrate}"; export STAGE1_PREFIX
STAGE="${SUBSTRATE_TOP}/dist-overlay/dist-freetype"

[ -f "${SUBSTRATE_TOP}/dist-overlay/dist-harfbuzz/usr/lib/pkgconfig/harfbuzz.pc" ] || {
    echo "freetype-harfbuzz: harfbuzz is not staged -- build contrib/harfbuzz first" >&2; exit 1; }

FREETYPE_WITH_HARFBUZZ=1 DESTDIR="${STAGE}" "${HERE}/../freetype/build.sh"

"${STAGE1_PREFIX}/bin/i386-unknown-substrate-readelf" -d "${STAGE}/usr/lib/libfreetype.so.6" \
    | grep -q 'Shared library: \[libharfbuzz\.so\.0\]' || {
    echo "freetype-harfbuzz: libfreetype.so.6 does not link libharfbuzz.so.0" >&2; exit 1; }

"${SUBSTRATE_TOP}/contrib/strip-staging.sh" "${STAGE}" freetype
"${SUBSTRATE_TOP}/scripts/sync-sysroot.sh" freetype
echo "==> freetype rebuilt with harfbuzz, staged at ${STAGE} and mirrored into the sysroot"
