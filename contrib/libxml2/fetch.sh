#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TB="libxml2-2.11.9.tar.xz"; URL="https://download.gnome.org/sources/libxml2/2.11/libxml2-2.11.9.tar.xz"; SHA512="d5c34ed56525f4c6b61d7055fe4219d7a3337077b4fb27081682e9f8350f1542b4476ac42f2754e590b371a4d9a00921cebf20c10b299371b05b8391e7fa7c33"
mkdir -p "${HERE}/build"; cd "${HERE}/build"
[ -f "${TB}" ] || curl -fSL -o "${TB}" "${URL}"
echo "${SHA512}  ${TB}" | sha512sum -c -
rm -rf "libxml2-2.11.9"; tar xf "${TB}"
