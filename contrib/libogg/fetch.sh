#!/bin/sh
# Auto: substrate audio-codec port.  See contrib/substrate-codec.sh.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
. "${SUBSTRATE_TOP}/contrib/substrate-codec.sh"
codec_fetch "libogg-1.3.5.tar.xz" "https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.xz" \
  "5d1cbc2a3a1fcf5543f5729bd5eb560cfc740c5d17a2492ead137970c45e6203ec1f5de536d77c4b73ece9e3b0046eaa9181c02a09de72ac7ae51b1fca1e1ee7" "libogg-1.3.5"
