#!/bin/sh
# contrib/font-adobe-75dpi/build.sh — stage the Adobe 75dpi BDF fonts.
#
# Upstream compiles BDF -> PCF with bdftopcf; substrate doesn't ship
# bdftopcf and libXfont reads BDF directly, so we stage the .bdf
# sources as-is (same approach as contrib/font-misc-misc) and generate
# a fonts.dir from each BDF's FONT line.  The X server's compiled
# default font path already lists /usr/share/fonts/X11/75dpi/, so the
# directory is picked up automatically once it contains a fonts.dir.
#
# These fonts provide -adobe-helvetica / -adobe-times / -adobe-courier,
# which twm (and other classic X clients) request by default; without
# them twm falls back to an empty fontset and renders menu/title text
# as tofu boxes under a UTF-8 locale.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.0.4"
DPI="75dpi"
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

# Derive ISO8859-1 (single-byte) variants from the ISO10646-1 masters.
#
# Critical for fontset clients (twm, etc.): the X11 en_US.UTF-8 locale's
# XLC_FONTSET binds its Latin/ASCII slots (fs0 = ISO8859-1:GL, fs1 =
# ISO8859-1:GR) to ISO8859-1 *single-byte* fonts.  If only the 2-byte
# ISO10646-1 fonts are present, libX11 substitutes one into the 1-byte
# slot; its is_xchar2b flag then mismatches the converter's single-byte
# output and XmbDrawString pairs the bytes into bogus XChar2b indices —
# every two characters render as one .notdef tofu box.  ISO8859-1 is
# row 0 of ISO10646-1, so the master already contains every glyph; we
# just keep the ENCODING 0..255 subset and relabel the registry.
derive_iso8859_1() {  # <src.bdf> <out.bdf>
    awk '
    function flush(){ if(buf!=""){ if(enc>=0 && enc<=255){ printf "%s", buf; n++ } buf=""; enc=-999 } }
    BEGIN{ inchar=0; enc=-999; buf="" }
    /^STARTCHAR/{ inchar=1; buf=$0 ORS; next }
    inchar && /^ENCODING/{ enc=$2; buf=buf $0 ORS; next }
    /^ENDCHAR/{ if(inchar){ buf=buf $0 ORS; flush(); inchar=0 } else print; next }
    inchar{ buf=buf $0 ORS; next }
    /^FONT /{ line=$0; sub(/-ISO10646-1[ \t\r]*$/,"-ISO8859-1",line); print line; next }
    /^CHARSET_REGISTRY/{ print "CHARSET_REGISTRY \"ISO8859\""; next }
    /^CHARSET_ENCODING/{ print "CHARSET_ENCODING \"1\""; next }
    /^CHARS /{ print "CHARS PLACEHOLDER"; next }
    { print }
    ' "$1" > "$2.raw"
    realn=$(grep -c '^STARTCHAR' "$2.raw")
    sed "s/^CHARS PLACEHOLDER/CHARS ${realn}/" "$2.raw" > "$2"
    rm -f "$2.raw"
}
derived=0
for f in "${DST}"/*.bdf; do
    # Only ISO10646-1 masters are convertible (skip e.g. adobe-fontspecific).
    grep -qE '^FONT .*-ISO10646-1[[:space:]]*$' "${f}" || continue
    out="${f%.bdf}-iso8859-1.bdf"
    derive_iso8859_1 "${f}" "${out}"
    derived=$((derived + 1))
done
echo "==> derived ${derived} ISO8859-1 variant fonts"

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
