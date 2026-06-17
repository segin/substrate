#!/bin/sh
# contrib/tde/dbus-1-tqt/fetch.sh — TDE TQt<->D-Bus binding (tdelibs dep).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VER="14.1.6"; TB="dbus-1-tqt-trinity-${VER}.tar.xz"
URL="https://mirror.ppa.trinitydesktop.org/trinity/releases/R${VER}/main/dependencies/${TB}"
SHA512="10578e0bc8c412f9064525815f972b38eed794c6f7114e7fc7373805c3b3e51bb08f52159dd3835c74a433397e1ba7e8e9d335881d03e30bbdd9859d72063e46"
mkdir -p "${HERE}/build"; cd "${HERE}/build"
[ -f "${TB}" ] || curl -fSL -o "${TB}" "${URL}"
echo "${SHA512}  ${TB}" | sha512sum -c -
rm -rf "dbus-1-tqt-trinity-${VER}"; tar xf "${TB}"
