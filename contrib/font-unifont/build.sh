#!/bin/sh
# contrib/font-unifont/build.sh — stage GNU Unifont as a last-resort font.
#
# Unifont has a glyph for every assigned code point in the Basic
# Multilingual Plane (unifont.otf) and covers much of the planes above it
# (unifont_upper.otf), drawn as 8x16 / 16x16 pixel glyphs.  fontconfig's
# stock 69-unifont.conf (already enabled) ranks it after the real families,
# so it is used only for characters nothing else has: CJK, Indic scripts,
# rare symbols -- a crude glyph instead of an empty box.
#
# Only those two of the release's precompiled fonts are installed.  The
# others are variants this system has no use for: unifont_jp (Japanese
# glyph forms), unifont_csur (ConScript private-use), unifont_t (Tolkien
# scripts), the *_sample fonts, and the BDF/PCF/PSF/hex sources.
#
# No compile step.  Installed without the version in the file name, so an
# update replaces the file instead of leaving two copies installed.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="18.0.01"
SRC="${HERE}/build/unifont-${VERSION}"
PRE="${SRC}/font/precompiled"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-font-unifont}"

[ -f "${PRE}/unifont-${VERSION}.otf" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${DESTDIR}"
FONTS="${DESTDIR}/usr/share/fonts/unifont"
DOC="${DESTDIR}/usr/share/doc/font-unifont"
mkdir -p "${FONTS}" "${DOC}"

install -m 0644 "${PRE}/unifont-${VERSION}.otf" "${FONTS}/unifont.otf"
install -m 0644 "${PRE}/unifont_upper-${VERSION}.otf" "${FONTS}/unifont_upper.otf"
install -m 0644 "${SRC}/COPYING" "${SRC}/OFL-1.1.txt" "${DOC}/"

echo "==> staged $(ls "${FONTS}" | wc -l) fonts under ${DESTDIR}"
