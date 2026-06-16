#!/bin/sh
# contrib/cjson/build.sh — cross-build cJSON for substrate.
# cJSON has no configure step; it's two translation units.  We compile them
# with the cross compiler straight into static archives and hand-write the
# headers + pkg-config files (upstream only generates the .pc via CMake).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.7.19"
TREE_DIR="${HERE}/build/cJSON-${VERSION}"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-cjson}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
CC=i386-unknown-substrate-gcc
AR=i386-unknown-substrate-ar
RANLIB=i386-unknown-substrate-ranlib
CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -std=c89 -fPIC -Wall"

# Upstream's CMake build ships the shared library as SOVERSION 1
# (libcjson.so.1 -> libcjson.so.1.7.x); match that so client DT_NEEDED entries
# read libcjson.so.1.
SOVER=1
FULLVER="${VERSION}"

cd "${TREE_DIR}"
echo "==> compile"
${CC} ${CFLAGS} -c cJSON.c        -o cJSON.o
${CC} ${CFLAGS} -c cJSON_Utils.c  -o cJSON_Utils.o
echo "==> archive (static)"
${AR} rcs libcjson.a       cJSON.o
${RANLIB} libcjson.a
${AR} rcs libcjson_utils.a cJSON_Utils.o cJSON.o
${RANLIB} libcjson_utils.a
echo "==> link (shared)"
${CC} -shared -fPIC -Wl,-soname,libcjson.so.${SOVER} \
      -o libcjson.so.${FULLVER} cJSON.o -lm
${CC} -shared -fPIC -Wl,-soname,libcjson_utils.so.${SOVER} \
      -o libcjson_utils.so.${FULLVER} cJSON_Utils.o cJSON.o -lm

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
inc="${DESTDIR}/usr/include/cjson"
lib="${DESTDIR}/usr/lib"
pc="${lib}/pkgconfig"
mkdir -p "${inc}" "${lib}" "${pc}"
install -m 0644 cJSON.h cJSON_Utils.h "${inc}/"
install -m 0644 libcjson.a libcjson_utils.a "${lib}/"
# Shared objects + the version/soname/linker symlink chain.
for base in libcjson libcjson_utils; do
    install -m 0755 "${base}.so.${FULLVER}" "${lib}/"
    ln -sf "${base}.so.${FULLVER}" "${lib}/${base}.so.${SOVER}"
    ln -sf "${base}.so.${SOVER}"   "${lib}/${base}.so"
done

# Hand-written pkg-config files (consumers ask for `libcjson`; the prefix is the
# on-target /usr where these install).
cat > "${pc}/libcjson.pc" <<EOF
prefix=/usr
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libcjson
Version: ${VERSION}
Description: Ultralightweight JSON parser in ANSI C
URL: https://github.com/DaveGamble/cJSON
Libs: -L\${libdir} -lcjson
Libs.private: -lm
Cflags: -I\${includedir} -I\${includedir}/cjson
EOF
cat > "${pc}/libcjson_utils.pc" <<EOF
prefix=/usr
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libcjson_utils
Version: ${VERSION}
Description: cJSON utility functions (JSON Pointer, Patch, Merge Patch)
URL: https://github.com/DaveGamble/cJSON
Requires: libcjson
Libs: -L\${libdir} -lcjson_utils
Cflags: -I\${includedir} -I\${includedir}/cjson
EOF
echo "==> Done.  cJSON staged at ${DESTDIR}/usr/{lib/libcjson*.a,include/cjson}"
