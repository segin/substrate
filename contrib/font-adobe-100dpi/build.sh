#!/bin/sh
# contrib/font-adobe-100dpi/build.sh — stage the Adobe 100dpi BDF fonts.
#
# Upstream compiles BDF -> PCF with bdftopcf; substrate doesn't ship
# bdftopcf and libXfont reads BDF directly, so we stage the .bdf
# sources as-is (same approach as contrib/font-misc-misc) and generate
# a fonts.dir from each BDF's FONT line.  The X server's compiled
# default font path already lists /usr/share/fonts/X11/100dpi/, so the
# directory is picked up automatically once it contains a fonts.dir.
#
# These fonts provide -adobe-helvetica / -adobe-times / -adobe-courier,
# which twm (and other classic X clients) request by default; without
# them twm falls back to an empty fontset and renders menu/title text
# as tofu boxes under a UTF-8 locale.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.0.4"
DPI="100dpi"
TREE_DIR="${HERE}/build/font-adobe-${DPI}-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-font-adobe-${DPI}}"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

DST="${DESTDIR}/usr/share/fonts/X11/${DPI}"
echo "==> staging Adobe ${DPI} BDF fonts into ${DST}"
rm -rf "${DESTDIR}"
mkdir -p "${DST}"

count=0
for f in "${TREE_DIR}"/*.bdf; do
    [ -f "${f}" ] || continue
    cp -a "${f}" "${DST}/"
    count=$((count + 1))
done
[ "${count}" -gt 0 ] || { echo "build.sh: no BDF files under ${TREE_DIR}" >&2; exit 1; }

# Generate fonts.dir:
#   <count>
#   <basename>\t<XLFD>
# The XLFD is the FONT line inside each BDF.
: >"${DST}/fonts.dir.tmp"
n=0
for f in "${DST}"/*.bdf; do
    bn=$(basename "${f}")
    xlfd=$(sed -n 's/^FONT[[:space:]][[:space:]]*\(.*\)$/\1/p' "${f}" | head -1)
    [ -n "${xlfd}" ] || { echo "build.sh: ${bn} has no FONT line, skipping" >&2; continue; }
    printf '%s\t%s\n' "${bn}" "${xlfd}" >>"${DST}/fonts.dir.tmp"
    n=$((n + 1))
done
{ printf '%d\n' "${n}"; cat "${DST}/fonts.dir.tmp"; } > "${DST}/fonts.dir"
rm -f "${DST}/fonts.dir.tmp"
cp -a "${DST}/fonts.dir" "${DST}/fonts.scale"
echo "==> generated fonts.dir with ${n} entries"
echo "==> dist staged at ${DESTDIR}"
