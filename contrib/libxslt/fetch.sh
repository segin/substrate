#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TB="libxslt-1.1.39.tar.xz"; URL="https://download.gnome.org/sources/libxslt/1.1/libxslt-1.1.39.tar.xz"; SHA512="c0c99dc63f8b2acb6cc3ad7ad684ffa2a427ee8d1740495cbf8a7c9b9c8679f96351b4b676c73ccc191014db4cb4ab42b9a0070f6295565f39dbc665c5c16f89"
mkdir -p "${HERE}/build"; cd "${HERE}/build"
[ -f "${TB}" ] || curl -fSL -o "${TB}" "${URL}"
echo "${SHA512}  ${TB}" | sha512sum -c -
rm -rf "libxslt-1.1.39"; tar xf "${TB}"
