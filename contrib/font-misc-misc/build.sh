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

# Derive ISO8859-1 (single-byte) variants from the ISO10646-1 masters —
# same recipe as the font-adobe-{75,100}dpi ports.  Without a 1-byte
# ISO8859-1 font, fontset clients (twm, dtwm/CDE titles) under
# LANG=en_US.UTF-8 get a 2-byte ISO10646-1 font dropped into the
# ISO8859-1:GL/GR slots; libX11's is_xchar2b then mismatches the
# single-byte converter output and XmbDrawString pairs bytes into bogus
# XChar2b indices — every two characters render as one .notdef tofu box.
# ISO8859-1 is row 0 of ISO10646-1, so the master already has every
# glyph; keep ENCODING 0..255 and relabel the registry.
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
    grep -qE '^FONT .*-ISO10646-1[[:space:]]*$' "${f}" || continue
    out="${f%.bdf}-iso8859-1.bdf"
    derive_iso8859_1 "${f}" "${out}"
    derived=$((derived + 1))
done
echo "==> derived ${derived} ISO8859-1 variant fonts"

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

# Emit fonts.alias mapping the common short names X clients ask for
# (fixed, 6x13, 9x15, 10x20, ...) to the actual XLFDs in fonts.dir.
# Without this, X clients calling XLoadFont("fixed") get a NULL font,
# and the X server with SetDefaultFont("fixed") FatalErrors at startup.
#
# These point at the ISO8859-1 (single-byte) variants derived above:
# the traditional "fixed" font IS 1-byte, and — critically — making the
# default/"fixed" font 1-byte is what keeps fontset clients (twm, dtwm/CDE
# window titles) from rendering tofu (see derive_iso8859_1 above).  The
# 2-byte ISO10646-1 masters are still in fonts.dir for Unicode-aware
# clients that ask for the full XLFD.  Matching is case-insensitive per
# the XLFD spec, so lowercase here matches the TitleCase fonts.dir entries.
cat >"${DST}/fonts.alias" <<'EOF'
!
! fonts.alias — short-name → XLFD lookups for X clients that ask
! for "fixed" / "9x15" / "10x20" etc.
!
fixed   -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso8859-1
6x13    -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso8859-1
6x10    -misc-fixed-medium-r-normal--10-100-75-75-c-60-iso8859-1
6x12    -misc-fixed-medium-r-semicondensed--12-110-75-75-c-60-iso8859-1
7x13    -misc-fixed-medium-r-normal--13-120-75-75-c-70-iso8859-1
7x14    -misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1
8x13    -misc-fixed-medium-r-normal--13-120-75-75-c-80-iso8859-1
9x15    -misc-fixed-medium-r-normal--15-140-75-75-c-90-iso8859-1
10x20   -misc-fixed-medium-r-normal--20-200-75-75-c-100-iso8859-1
EOF

echo "==> dist staged at ${DESTDIR}"
