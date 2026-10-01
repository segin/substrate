#!/bin/sh
# contrib/font-liberation/build.sh — stage the Liberation TrueType fonts.
#
# Liberation Sans, Serif and Mono have the same advance widths as Arial,
# Times New Roman and Courier New, so text laid out for those fonts does
# not reflow.  fontconfig's stock 30-metric-aliases.conf (already enabled)
# maps requests for the Microsoft names to them; nothing to configure here.
# No compile step: the release ships the .ttf files.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.1.5"
SRC="${HERE}/build/liberation-fonts-ttf-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-font-liberation}"

[ -d "${SRC}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${DESTDIR}"
FONTS="${DESTDIR}/usr/share/fonts/liberation"
DOC="${DESTDIR}/usr/share/doc/font-liberation"
mkdir -p "${FONTS}" "${DOC}"

install -m 0644 "${SRC}"/*.ttf "${FONTS}/"
install -m 0644 "${SRC}/LICENSE" "${SRC}/AUTHORS" "${SRC}/README.md" "${DOC}/"

echo "==> staged $(ls "${FONTS}" | wc -l) fonts under ${DESTDIR}"
