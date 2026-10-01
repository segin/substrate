#!/bin/sh
# contrib/font-go/build.sh — stage the Go fonts.
#
# Go Regular/Medium/Bold (+ italics), Go Smallcaps and Go Mono: a compact,
# readable family designed by Bigelow & Holmes for the Go project, under
# Go's BSD licence.  Mostly useful as a monospace alternative to DejaVu
# Sans Mono.  No compile step.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="v0.46.0"
SRC="${HERE}/build/golang.org/x/image@${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-font-go}"

[ -d "${SRC}/font/gofont/ttfs" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${DESTDIR}"
FONTS="${DESTDIR}/usr/share/fonts/go"
DOC="${DESTDIR}/usr/share/doc/font-go"
mkdir -p "${FONTS}" "${DOC}"

install -m 0644 "${SRC}"/font/gofont/ttfs/*.ttf "${FONTS}/"
install -m 0644 "${SRC}/LICENSE" "${SRC}/PATENTS" "${DOC}/"
install -m 0644 "${SRC}/font/gofont/ttfs/README" "${DOC}/README"

echo "==> staged $(ls "${FONTS}" | wc -l) fonts under ${DESTDIR}"
