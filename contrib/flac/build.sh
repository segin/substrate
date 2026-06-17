#!/bin/sh
# Auto: substrate audio-codec port.  See contrib/substrate-codec.sh.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
. "${SUBSTRATE_TOP}/contrib/substrate-codec.sh"
[ -d "${HERE}/build/flac-1.4.3" ] || { echo 'run ./fetch.sh first' >&2; exit 1; }
codec_build flac flac-1.4.3 --disable-programs --disable-examples --disable-doxygen-docs --disable-stack-smash-protection --with-ogg="${SR}"
