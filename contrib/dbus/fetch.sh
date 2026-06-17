#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TB="dbus-1.14.10.tar.xz"; URL="https://dbus.freedesktop.org/releases/dbus/dbus-1.14.10.tar.xz"; SHA512="775b708326059692937acb69d4ce1a89e69878501166655b5d1b1628ac31b50dd53d979d93c84e57f95e90b15e25aa33893e51a7421d3537e9c2f02b1b91bfae"
mkdir -p "${HERE}/build"; cd "${HERE}/build"
[ -f "${TB}" ] || curl -fSL -o "${TB}" "${URL}"
echo "${SHA512}  ${TB}" | sha512sum -c -
rm -rf "dbus-1.14.10"; tar xf "${TB}"
