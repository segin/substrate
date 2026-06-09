#!/bin/sh
# contrib/font-bh-lucida/fetch.sh — X.Org B&H Lucida bitmap fonts
# (Lucida, LucidaBright, LucidaSans, LucidaTypewriter), 75dpi + 100dpi.
# CDE's dtcm (and other CDE apps) request -b&h-lucida* fontsets; without
# them the FontSet build fails and drawing the broken set CRASHES Xfbdev
# (taking the evdev grab down with it -> frozen desktop).  Source-only
# fetch; build.sh stages the BDFs.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
base="https://www.x.org/releases/individual/font"
# tarball  sha256
set -- \
  font-bh-100dpi-1.0.4                  fd8f5efe8491faabdd2744808d3d4eafdae5c83e617017c7fddd2716d049ab1e \
  font-bh-75dpi-1.0.4                   6026d8c073563dd3cbb4878d0076eed970debabd21423b3b61dd90441b9e7cda \
  font-bh-lucidatypewriter-100dpi-1.0.4 76ec09eda4094a29d47b91cf59c3eba229c8f7d1ca6bae2abbb3f925e33de8f2 \
  font-bh-lucidatypewriter-75dpi-1.0.4  864e2c39ac61f04f693fc2c8aaaed24b298c2cd40283cec12eee459c5635e8f5
while [ $# -ge 2 ]; do
  name="$1"; sha="$2"; shift 2
  t="${name}.tar.xz"
  if [ ! -f "$t" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch: $t missing" >&2; exit 1; }
    echo "==> Fetching ${base}/${t}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "$t" "${base}/${t}"; else wget -O "$t" "${base}/${t}"; fi
  fi
  echo "${sha}  ${t}" | sha256sum -c -
  [ -d "${name}" ] || tar xf "$t"
done
echo "==> font-bh-lucida ready"
