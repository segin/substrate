#!/bin/sh
# contrib/font-bh-lucida/build.sh — stage the B&H Lucida BDF fonts.
# Mirrors contrib/font-adobe-100dpi: libXfont reads BDF directly (no
# bdftopcf), and the 1.0.4 BDFs are ISO10646-1, so we derive ISO8859-1
# single-byte variants for the en_US.UTF-8 fontset's Latin slot.  The
# final per-directory fonts.dir is regenerated at image-assembly time
# (build-rootfs.sh finalize_x_fonts) since adobe + bh share 100dpi/75dpi.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
  p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
  SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-font-bh-lucida}"
B="${HERE}/build"
[ -d "${B}/font-bh-100dpi-1.0.4" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

derive_iso8859_1() {
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
  { print }' "$1" > "$2.raw"
  realn=$(grep -c '^STARTCHAR' "$2.raw"); sed "s/^CHARS PLACEHOLDER/CHARS ${realn}/" "$2.raw" > "$2"; rm -f "$2.raw"
}

rm -rf "${DESTDIR}"
for dpi in 100dpi 75dpi; do
  DST="${DESTDIR}/usr/share/fonts/X11/${dpi}"; mkdir -p "${DST}"
  for pkg in "font-bh-${dpi}-1.0.4" "font-bh-lucidatypewriter-${dpi}-1.0.4"; do
    for f in "${B}/${pkg}"/*.bdf; do
      [ -f "$f" ] || continue; bn=$(basename "$f"); cp -a "$f" "${DST}/${bn}"
      grep -qE '^FONT .*-ISO10646-1[[:space:]]*$' "${DST}/${bn}" && \
        derive_iso8859_1 "${DST}/${bn}" "${DST}/${bn%.bdf}-iso8859-1.bdf" || true
    done
  done
  echo "==> staged ${dpi}: $(ls "${DST}"/*.bdf | wc -l) BDFs"
done
echo "==> font-bh-lucida staged at ${DESTDIR}"
