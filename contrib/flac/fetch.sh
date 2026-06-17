#!/bin/sh
# Auto: substrate audio-codec port.  See contrib/substrate-codec.sh.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
. "${SUBSTRATE_TOP}/contrib/substrate-codec.sh"
codec_fetch "flac-1.4.3.tar.xz" "https://downloads.xiph.org/releases/flac/flac-1.4.3.tar.xz" \
  "3cf095720bd590a588be8ccbe187d22e7a1c60ab85b1d510ce5e8a22ab0a51827b9acfeaad59bbd645a17d1f200f559255a640101b0330709a164306c0e9709e" "flac-1.4.3"
