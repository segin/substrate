#!/bin/sh
#
# build.sh — configure + build + install GNU Libtool for substrate.
#
# Cross build: libltdl is a C library for the target, and the installed
# `libtool` script is configured for the target's compiler.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.6.2"
TREE_DIR="${HERE}/build/libtool-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-libtool}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }


# Patch 0002 teaches m4/libtool.m4 (the macro installed for packages to
# use) about substrate.  Keep its tarball timestamp so make doesn't try to
# regenerate aclocal.m4 and configure with whatever autotools the host
# has, and fix up the shipped configure scripts the same way instead.
touch -r "${TREE_DIR}/m4/ltoptions.m4" "${TREE_DIR}/m4/libtool.m4"
sh "${SUBSTRATE_TOP}/contrib/substrate-libtool-shared.sh" \
    "${TREE_DIR}/configure" "${TREE_DIR}/libltdl/configure"

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

# The installed libtool and libtoolize record tool paths found on the
# build host; give them substrate's instead:
# lt_cv_sys_lib_dlsearch_path_spec: otherwise configure reads the build
#   host's /etc/ld.so.conf.  /lib and /usr/lib are ld.so's default
#   search directories.
# ac_cv_path_GREP: substrate's grep is /bin/grep (EGREP and FGREP follow
#   as "grep -E" / "grep -F").  A missing grep makes libtoolize ignore
#   AC_CONFIG_AUX_DIR and drop ltmain.sh in the wrong directory.
# lt_cv_truncate_bin: substrate has no dd; this is libtool's own
#   fallback for reading a file's header.
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --enable-ltdl-install \
    lt_cv_sys_lib_dlsearch_path_spec="/lib /usr/lib" \
    ac_cv_path_GREP=/bin/grep \
    lt_cv_truncate_bin="/usr/bin/sed -e 4q" \
    CFLAGS="-O2 -g -march=i486 -mtune=i486"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
find "${DESTDIR}" -name '*.la' -delete

# The installed libtool script records the cross toolchain it was
# configured with.  Point it at the native one: the target has
# i386-unknown-substrate-gcc/g++ but unprefixed binutils, and keeps crt
# objects and libraries in /usr/lib (gcc's own under /usr/lib/gcc).
t=i386-unknown-substrate
sed -i \
    -e "s#${STAGE1_PREFIX}/lib/gcc/${t}/\([^/]*\)/\.\./\.\./\.\./\.\./${t}/lib#/usr/lib#g" \
    -e "s#${STAGE1_PREFIX}/lib/gcc/#/usr/lib/gcc/#g" \
    -e "s#${STAGE1_PREFIX}/${t}/lib#/usr/lib#g" \
    -e "s#${STAGE1_PREFIX}/${t}/bin/ld#ld#g" \
    -e "s#${STAGE1_PREFIX}/bin/${t}-nm#nm#g" \
    -e "s#${SUBSTRATE_TOP}/dist/usr/lib#/usr/lib#g" \
    -e "s#${SUBSTRATE_TOP}/dist/lib#/lib#g" \
    -e "s#\([\" ]\)${t}-\(objdump\|nm\|ar\|strip\|ranlib\)\([\" ]\)#\1\2\3#g" \
    "${DESTDIR}/usr/bin/libtool"
if grep -n -e "${STAGE1_PREFIX}" -e "${SUBSTRATE_TOP}" -e "${t}-\(objdump\|nm\|ar\|strip\|ranlib\)" \
        -e '/usr/bin/grep' -e '/usr/bin/dd' \
        "${DESTDIR}/usr/bin/libtool" "${DESTDIR}/usr/bin/libtoolize"; then
    echo "build.sh: host paths left in the installed libtool" >&2
    exit 1
fi

echo "==> Done.  Staged at ${DESTDIR}/usr/bin/libtool"
