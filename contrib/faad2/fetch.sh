#!/bin/sh
# contrib/faad2/fetch.sh — AAC decoder (libfaad).  CMake build.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VER="2.11.1"; TB="faad2-${VER}.tar.gz"
URL="https://github.com/knik0/faad2/archive/refs/tags/${VER}.tar.gz"
SHA512="b8f17680610b2f47344ea52b54412a02810a85eaf9d4c91b97ca09b2c6415c62d4af1b0771bfcacb9dfee400ed34504c0bd3c28369921c0392b3809e7de46ec5"
mkdir -p "${HERE}/build"; cd "${HERE}/build"
[ -f "${TB}" ] || curl -fSL -o "${TB}" "${URL}"
echo "${SHA512}  ${TB}" | sha512sum -c -
rm -rf "faad2-${VER}"; tar xf "${TB}"
