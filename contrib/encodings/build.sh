#!/bin/sh
# contrib/encodings/build.sh — stage encoding files directly.
#
# encodings is upstream-configured to require mkfontscale at build
# time to produce encodings.dir.  mkfontscale pulls in freetype +
# libpng + harfbuzz, none of which we want to drag in just to make
# an index of .enc files.  Stage the .enc files directly; generate
# encodings.dir with sed (the format is trivial).

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.1.0"
TREE_DIR="${HERE}/build/encodings-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-encodings}"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

DST="${DESTDIR}/usr/share/fonts/X11/encodings"
LARGE="${DST}/large"
echo "==> staging encodings into ${DST}"
rm -rf "${DESTDIR}"
mkdir -p "${LARGE}"

# All top-level .enc files (latin / cyrillic / greek / arabic / hebrew / ...)
for f in "${TREE_DIR}"/*.enc.gz "${TREE_DIR}"/*.enc; do
    [ -f "${f}" ] || continue
    cp -a "${f}" "${DST}/"
done

# Large encodings (CJK code-page tables) under large/.
for f in "${TREE_DIR}"/large/*.enc.gz "${TREE_DIR}"/large/*.enc; do
    [ -f "${f}" ] || continue
    cp -a "${f}" "${LARGE}/"
done

# Generate encodings.dir — what mkfontscale would have produced.  Format:
#   <count>
#   <encoding-name>: <filename>
gen_dir() {
    out="$1"; shift
    count=0
    : > "${out}.tmp"
    for f in "$@"; do
        bn=$(basename "${f}")
        case "${bn}" in *.gz)
            name=$(zcat "${f}" | sed -n 's/^STARTENCODING[[:space:]][[:space:]]*\([^[:space:]]*\).*/\1/p' | head -1)
            ;; *)
            name=$(sed -n 's/^STARTENCODING[[:space:]][[:space:]]*\([^[:space:]]*\).*/\1/p' "${f}" | head -1)
            ;;
        esac
        [ -n "${name}" ] || continue
        echo "${name}: ${bn}" >> "${out}.tmp"
        count=$((count + 1))
    done
    { echo "${count}"; sort "${out}.tmp"; } > "${out}"
    rm -f "${out}.tmp"
}
gen_dir "${DST}/encodings.dir" "${DST}"/*.enc.gz "${DST}"/*.enc 2>/dev/null
gen_dir "${LARGE}/encodings.dir" "${LARGE}"/*.enc.gz "${LARGE}"/*.enc 2>/dev/null

echo "==> Done.  encodings staged at ${DST}"
echo "    top:   $(head -1 "${DST}/encodings.dir") files"
echo "    large: $(head -1 "${LARGE}/encodings.dir") files"
