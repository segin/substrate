#!/bin/sh
# Auto: substrate audio-codec port.  See contrib/substrate-codec.sh.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
. "${SUBSTRATE_TOP}/contrib/substrate-codec.sh"
codec_fetch "opus-1.5.2.tar.gz" "https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz" \
  "78d963cd56d5504611f111e2b3606e236189a3585d65fae1ecdbec9bf4545632b1956f11824328279a2d1ea2ecf441ebc11e455fb598d20a458df15185e95da4" "opus-1.5.2"
