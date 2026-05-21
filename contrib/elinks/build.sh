#!/bin/sh
#
# contrib/elinks/build.sh — cross-compile the ELinks text browser
# for substrate, with the QuickJS ECMAScript backend.
#
# Meson is a host-side build tool: it runs on the build host with
# the host's Python, reads the generated substrate cross file, and
# emits a Ninja build that cross-compiles elinks.  Nothing here is
# installed on substrate.
#
# Depends on these being staged in the cross-toolchain sysroot:
#   quickjs, libcss, libdom, libhubbub, libparserutils,
#   libwapcaplet, openssl, zlib.
#
# Env: STAGE1_PREFIX (/opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="0.19.1"
TREE_DIR="${HERE}/build/elinks-${VERSION}"
BUILD_SUB="${TREE_DIR}/build-substrate"
CROSS_FILE="${HERE}/build/substrate-cross.ini"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-elinks}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SYSROOT="${STAGE1_PREFIX}/i386-unknown-substrate"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -f "${SYSROOT}/lib/pkgconfig/libdom.pc" ] || {
    echo "build.sh: libdom not staged in the sysroot — build the NetSurf libs first" >&2
    exit 1; }

# --- generate the Meson cross file --------------------------------
cat > "${CROSS_FILE}" <<EOF
[binaries]
c = 'i386-unknown-substrate-gcc'
cpp = 'i386-unknown-substrate-g++'
ar = 'i386-unknown-substrate-ar'
strip = 'i386-unknown-substrate-strip'
pkg-config = 'pkg-config'

[host_machine]
system = 'substrate'
cpu_family = 'x86'
cpu = 'i586'
endian = 'little'

[properties]
pkg_config_libdir = '${SYSROOT}/lib/pkgconfig'

[built-in options]
c_args = ['-march=i586', '-mtune=i686', '-fno-pie']
c_link_args = ['-fno-pie']
cpp_args = ['-march=i586', '-mtune=i686', '-fno-pie']
cpp_link_args = ['-fno-pie']
EOF

# --- configure ----------------------------------------------------
rm -rf "${BUILD_SUB}"
echo "==> meson setup"
meson setup "${BUILD_SUB}" "${TREE_DIR}" \
    --cross-file "${CROSS_FILE}" \
    --prefix=/usr \
    -Db_pie=false \
    -Dquickjs=true \
    -Dmujs=false -Dspidermonkey=false -Dsm-scripting=false \
    -Dopenssl=true -Dgnutls=false -Dzlib=true \
    -Dipv6=false \
    -Dtre=false -Didn=false -Dgpm=false -Dbacktrace=false \
    -Dnls=false -Dgettext=false -Dxbel=false \
    -Dlibcurl=false -Dsftp=false -Dbittorrent=false -Dx=false \
    -Dapidoc=false -Ddoc=false -Dhtmldoc=false -Dpdfdoc=false \
    -Dtest=false -Dtest-js=false

# --- build --------------------------------------------------------
echo "==> ninja"
ninja -C "${BUILD_SUB}" -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
DESTDIR="${DESTDIR}" ninja -C "${BUILD_SUB}" install

echo "==> Done.  /usr/bin/elinks staged under ${DESTDIR}"
