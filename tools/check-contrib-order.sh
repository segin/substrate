#!/bin/sh
#
# tools/check-contrib-order.sh — verify DEFAULT_CONTRIB is topologically sane.
#
# build.sh builds contrib ports in list order and each one links against
# what the ports before it staged.  Get the order wrong and the failure
# arrives at that port's configure, however many hours in that happens to
# be:
#
#     checking for disasterparty... no
#     configure: error: disasterparty not found.
#
# That one cost a 2-hour run.  It was invisible locally because
# disasterparty had been built here by hand months earlier, so motifgpt
# found it no matter where in the list it sat -- the same "this machine has
# state a clean runner does not" that every other bring-up failure was.
#
# The constraints below are not guesses.  They were read out of each port's
# configure.ac (PKG_CHECK_MODULES / PKG_CHECK_EXISTS), which is the build
# system's own statement of what it needs, rather than out of build.sh
# header comments -- contrib/motifgpt's claim no dependencies at all.
#
# Usage:  check-contrib-order.sh [path/to/build.sh]
#
# Exit 0 if every constraint holds, 1 otherwise.  Cheap enough to run from
# build.sh's pre-flight, which is the point: a second here instead of hours
# there.

set -eu

BUILD_SH="${1:-$(dirname "$0")/../build.sh}"
[ -f "$BUILD_SH" ] || { echo "$0: no such file: $BUILD_SH" >&2; exit 2; }

ORDER=$(sed -n 's/^DEFAULT_CONTRIB="\(.*\)"$/\1/p' "$BUILD_SH")
[ -n "$ORDER" ] || { echo "$0: could not read DEFAULT_CONTRIB from $BUILD_SH" >&2; exit 2; }

# "before:after" — every pair where `after` reads something `before` stages.
CONSTRAINTS="
cjson:disasterparty
cjson:motifgpt
disasterparty:motifgpt
libffi:glib2
glib2:atk
glib2:harfbuzz
glib2:gdk-pixbuf
glib2:pango
glib2:gtk2
cairo:harfbuzz
cairo:pango
cairo:gtk2
fribidi:pango
harfbuzz:pango
atk:gtk2
gdk-pixbuf:gtk2
pango:gtk2
gtk2:hexchat
glib1:gtk1
libparserutils:libcss
libparserutils:libdom
libparserutils:libhubbub
libwapcaplet:libcss
libwapcaplet:libdom
libwapcaplet:libhubbub
libogg:libvorbis
libogg:flac
libogg:speex
libopus:sox
sdl3:sdl2-compat
sdl2-compat:sdl12-compat
sdl2-compat:psymp3
taglib:psymp3
faad2:psymp3
spandsp:psymp3
speex:psymp3
libogg:psymp3
libxml2:libxslt
libXfixes:libXi
libXi:libXtst
libXtst:tde
dbus:tde
file:tde
glib2:tde
libxslt:tde
expat:fontconfig
freetype:libXft
fontconfig:libXft
libX11:libXext
libXext:libXrender
zlib:libpng
libpng:freetype
xorgproto:libX11
libxcb:libX11
libXau:libxcb
motif:cde
libXScrnSaver:cde
"

pos() {
    # 1-based index of $1 in ORDER, empty if absent
    echo "$ORDER" | tr ' ' '\n' | grep -n -x -- "$1" 2>/dev/null | cut -d: -f1 | head -1
}

fail=0
checked=0
skipped=0
for pair in $CONSTRAINTS; do
    before=${pair%%:*}
    after=${pair##*:}
    pb=$(pos "$before")
    pa=$(pos "$after")
    # A constraint about a port that is not in the list is not a failure --
    # the list is allowed to be a subset (ONLY=... builds one port).
    if [ -z "$pb" ] || [ -z "$pa" ]; then
        skipped=$((skipped + 1))
        continue
    fi
    checked=$((checked + 1))
    if [ "$pb" -ge "$pa" ]; then
        printf '  %s (position %s) must come before %s (position %s)\n' \
               "$before" "$pb" "$after" "$pa" >&2
        fail=$((fail + 1))
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "check-contrib-order.sh: $fail ordering constraint(s) violated" >&2
    exit 1
fi

echo "==> contrib order: $checked constraints hold${skipped:+, $skipped not applicable}"
exit 0
