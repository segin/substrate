#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TB="file-5.45.tar.gz"; URL="https://astron.com/pub/file/file-5.45.tar.gz"; SHA512="12611a59ff766c22a55db4b4a9f80f95a0a2e916a1d8593612c6ead32c247102a8fdc23693c6bf81bda9b604d951a62c0051e91580b1b79e190a3504c0efc20a"
mkdir -p "${HERE}/build"; cd "${HERE}/build"
[ -f "${TB}" ] || curl -fSL -o "${TB}" "${URL}"
echo "${SHA512}  ${TB}" | sha512sum -c -
rm -rf "file-5.45"; tar xf "${TB}"
