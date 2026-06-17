#!/bin/sh
# Auto: substrate audio-codec port.  See contrib/substrate-codec.sh.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
. "${SUBSTRATE_TOP}/contrib/substrate-codec.sh"
[ -d "${HERE}/build/libvorbis-1.3.7" ] || { echo 'run ./fetch.sh first' >&2; exit 1; }
codec_build libvorbis libvorbis-1.3.7 --with-ogg="${SR}"
