#!/bin/sh
#
# build.sh — configure + build + install nginx for substrate.
#
# nginx's configure is a bespoke shell script (not autoconf) that
# compiles AND RUNS feature-probe binaries.  Cross-compiling to
# substrate can't run those, so we drive it through nginx's
# --crossbuild mechanism (sets NGX_PLATFORM) plus two patches:
#   0001 presets type sizes, 0002 assumes run-tests pass.
#
# Depends on contrib/zlib (gzip filter) and contrib/openssl
# (http_ssl_module), both staged first.
#
# Env:
#   STAGE1_PREFIX   default /opt/substrate
#   ZLIB_STAGE      default ${SUBSTRATE_TOP}/dist-overlay/dist-zlib
#   OPENSSL_STAGE   default ${SUBSTRATE_TOP}/dist-overlay/dist-openssl
#   DESTDIR         default ${SUBSTRATE_TOP}/dist-overlay/dist-nginx
#   JOBS            default `nproc`

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.26.2"
TREE_DIR="${HERE}/build/nginx-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${ZLIB_STAGE:=${SUBSTRATE_TOP}/dist-overlay/dist-zlib}"
: "${OPENSSL_STAGE:=${SUBSTRATE_TOP}/dist-overlay/dist-openssl}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-nginx}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

CROSS=i386-unknown-substrate

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
command -v "${CROSS}-gcc" >/dev/null 2>&1 || {
    echo "build.sh: ${CROSS}-gcc not on PATH (STAGE1_PREFIX=${STAGE1_PREFIX})" >&2
    exit 1
}
[ -d "${ZLIB_STAGE}/usr/include" ] || {
    echo "build.sh: zlib not staged at ${ZLIB_STAGE} — build contrib/zlib first" >&2
    exit 1
}
[ -d "${OPENSSL_STAGE}/usr/include/openssl" ] || {
    echo "build.sh: OpenSSL not staged at ${OPENSSL_STAGE} — build contrib/openssl first" >&2
    exit 1
}

# nginx configure honors --with-cc-opt / --with-ld-opt for the
# compiler/linker flags it threads through every translation unit and
# the final link.  Point them at the staged zlib + openssl trees.
CC_OPT="-march=i486 -mtune=i486 -O2 -g -fno-pie"
# nginx compiles with -Werror; appended after its own flags, -Wno-error
# demotes it.  GCC 16 also promotes several legacy-C warnings to errors
# by default (independent of -Werror) -- demote those explicitly too, the
# same set the xorg-server port needs.  Benign on substrate, e.g.
# select()'s `restrict`-qualified timeval arg.
CC_OPT="${CC_OPT} -Wno-error -Wno-incompatible-pointer-types"
CC_OPT="${CC_OPT} -Wno-int-conversion -Wno-return-mismatch"
CC_OPT="${CC_OPT} -Wno-implicit-function-declaration"
CC_OPT="${CC_OPT} -I${ZLIB_STAGE}/usr/include -I${OPENSSL_STAGE}/usr/include"
LD_OPT="-fno-pie -L${ZLIB_STAGE}/usr/lib -L${OPENSSL_STAGE}/usr/lib"
LD_OPT="${LD_OPT} -Wl,--copy-dt-needed-entries"

cd "${TREE_DIR}"

# nginx leaves objs/ from a prior run; wipe for a clean configure.
rm -rf objs

echo "==> configure (crossbuild=substrate:1.0:i386)"
./configure \
    --crossbuild=substrate:1.0:i386 \
    --prefix=/usr/share/nginx \
    --sbin-path=/usr/sbin/nginx \
    --modules-path=/usr/lib/nginx/modules \
    --conf-path=/etc/nginx/nginx.conf \
    --pid-path=/var/run/nginx.pid \
    --lock-path=/var/run/nginx.lock \
    --error-log-path=/var/log/nginx/error.log \
    --http-log-path=/var/log/nginx/access.log \
    --http-client-body-temp-path=/var/cache/nginx/client_temp \
    --http-proxy-temp-path=/var/cache/nginx/proxy_temp \
    --http-fastcgi-temp-path=/var/cache/nginx/fastcgi_temp \
    --http-uwsgi-temp-path=/var/cache/nginx/uwsgi_temp \
    --http-scgi-temp-path=/var/cache/nginx/scgi_temp \
    --user=nobody --group=nobody \
    --with-cc="${CROSS}-gcc" \
    --with-cpp="${CROSS}-gcc -E" \
    --with-poll_module \
    --with-select_module \
    --without-pcre \
    --without-http_rewrite_module \
    --with-http_ssl_module \
    --with-http_gzip_static_module \
    --with-cc-opt="${CC_OPT}" \
    --with-ld-opt="${LD_OPT}"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  /usr/sbin/nginx staged under ${DESTDIR}"
