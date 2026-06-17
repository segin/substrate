#!/bin/sh
# contrib/sox/fetch.sh — SoX (Sound eXchange) audio processor.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TB="sox-14.4.2.tar.bz2"
URL="https://downloads.sourceforge.net/project/sox/sox/14.4.2/${TB}"
SHA512="424b80e9fff43864b0581fea7a231b8308bdebb2aee0b97cc40eeaa347c093e94bcd0111e8b431e7bfe88b3c1133660ede42b6b49d14555ea0626c2c0ffa308e"
mkdir -p "${HERE}/build"; cd "${HERE}/build"
[ -f "${TB}" ] || curl -fSL -o "${TB}" "${URL}"
echo "${SHA512}  ${TB}" | sha512sum -c -
rm -rf sox-14.4.2; tar xf "${TB}"
echo "==> Applying patch series"
while IFS= read -r p; do [ -z "${p}" ] && continue; patch -p1 -d sox-14.4.2 < "${HERE}/patches/${p}"; done < "${HERE}/series"
