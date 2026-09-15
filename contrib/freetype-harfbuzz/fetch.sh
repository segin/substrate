#!/bin/sh
# contrib/freetype-harfbuzz/fetch.sh -- the second FreeType pass reuses
# contrib/freetype's source tree; see build.sh.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "${HERE}/../freetype/fetch.sh" "$@"
