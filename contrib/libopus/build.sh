#!/bin/sh
# Auto: substrate audio-codec port.  See contrib/substrate-codec.sh.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
. "${SUBSTRATE_TOP}/contrib/substrate-codec.sh"
[ -d "${HERE}/build/opus-1.5.2" ] || { echo 'run ./fetch.sh first' >&2; exit 1; }
codec_build libopus opus-1.5.2 --disable-extra-programs --disable-doc --disable-stack-protector
