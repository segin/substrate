#!/bin/sh
# contrib/font-misc-misc/build.sh — stage misc-fixed BDF fonts.
#
# Upstream's build system runs bdftopcf to compile BDF to PCF.
# substrate doesn't ship bdftopcf yet, and libXfont reads BDF
# directly (just slower than PCF — fine for the half-dozen fonts
# we use), so we stage the .bdf sources as-is and generate a
# fonts.dir entry pointing at them.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.1.3"
TREE_DIR="${HERE}/build/font-misc-misc-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-font-misc-misc}"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

DST="${DESTDIR}/usr/share/fonts/X11/misc"
echo "==> staging BDF fonts into ${DST}"
rm -rf "${DESTDIR}"
mkdir -p "${DST}"

# Copy every .bdf (handling both top-level and any subdirectory; upstream
# 1.1.3 puts them at the root of the extracted tree).
count=0
for f in "${TREE_DIR}"/*.bdf; do
    [ -f "${f}" ] || continue
    cp -a "${f}" "${DST}/"
    count=$((count + 1))
done

if [ "${count}" -eq 0 ]; then
    echo "build.sh: no BDF files found under ${TREE_DIR}" >&2
    exit 1
fi

# Generate fonts.dir.  Format:
#   <count>
#   <basename>     <XLFD>
# The XLFD is on the FONT line inside each BDF.
{
    : >"${DST}/fonts.dir.tmp"
    n=0
    for f in "${DST}"/*.bdf; do
        bn=$(basename "${f}")
        xlfd=$(sed -n 's/^FONT[[:space:]][[:space:]]*\(.*\)$/\1/p' "${f}" | head -1)
        if [ -z "${xlfd}" ]; then
            echo "build.sh: ${bn} has no FONT line, skipping" >&2
            continue
        fi
        printf '%s\t%s\n' "${bn}" "${xlfd}" >>"${DST}/fonts.dir.tmp"
        n=$((n + 1))
    done
    {
        printf '%d\n' "${n}"
        cat "${DST}/fonts.dir.tmp"
    } > "${DST}/fonts.dir"
    rm -f "${DST}/fonts.dir.tmp"
    cp -a "${DST}/fonts.dir" "${DST}/fonts.scale"
    echo "==> generated fonts.dir with ${n} entries"
}

echo "==> dist staged at ${DESTDIR}"
