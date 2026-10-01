#!/bin/sh
# contrib/font-dejavu/build.sh — stage the DejaVu TrueType fonts.
#
# Nothing to compile: the release ships the .ttf files.  Installs them under
# /usr/share/fonts/dejavu, where fontconfig's <dir>/usr/share/fonts</dir>
# finds them, plus DejaVu's own fontconfig rules (enabled through conf.d the
# way contrib/fontconfig enables its stock ones) and the licence.
#
# fontconfig's 60-latin.conf already lists DejaVu first for sans-serif,
# serif and monospace, so every Xft / cairo / Pango client switches to it
# with no further configuration.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.37"
SRC="${HERE}/build/dejavu-fonts-ttf-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-font-dejavu}"

[ -d "${SRC}/ttf" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${DESTDIR}"
FONTS="${DESTDIR}/usr/share/fonts/dejavu"
AVAIL="${DESTDIR}/usr/share/fontconfig/conf.avail"
CONFD="${DESTDIR}/etc/fonts/conf.d"
DOC="${DESTDIR}/usr/share/doc/font-dejavu"
mkdir -p "${FONTS}" "${AVAIL}" "${CONFD}" "${DOC}"

install -m 0644 "${SRC}"/ttf/*.ttf "${FONTS}/"
for c in "${SRC}"/fontconfig/*.conf; do
    bn=$(basename "${c}")
    install -m 0644 "${c}" "${AVAIL}/${bn}"
    ln -sf "/usr/share/fontconfig/conf.avail/${bn}" "${CONFD}/${bn}"
done
install -m 0644 "${SRC}/LICENSE" "${SRC}/AUTHORS" "${SRC}/README.md" "${DOC}/"

echo "==> staged $(ls "${FONTS}" | wc -l) fonts and" \
     "$(ls "${CONFD}" | wc -l) fontconfig rules under ${DESTDIR}"
