#!/bin/sh
# contrib/motifgpt/build.sh — cross-build motifgpt for substrate.
# Produces the motifgpt binary (Motif GUI LLM chat client) + its loadable
# plugins.  Links the Motif + X toolkit stack over disasterparty.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
COMMIT="7d2cbc9a063cc12bcde93d7f2d0a0eef09a0cf72"
TREE_DIR="${HERE}/build/motifgpt-${COMMIT}"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-motifgpt}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
SYSROOT="${STAGE1_PREFIX}/i386-unknown-substrate"

# pkg-config search pinned at the substrate dist trees: motifgpt + its deps.
PKGP=""; CPP=""; LDF=""
for d in motif disasterparty cjson curl openssl zlib \
         libX11 libXext libXt libXmu libXpm libICE libSM libxcb libXau xorgproto; do
    st="${SUBSTRATE_TOP}/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib"
done
export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF}"

cd "${TREE_DIR}"
[ -f Makefile ] && make distclean >/dev/null 2>&1 || true
CACHE="${TREE_DIR}/substrate.cache"
grep -v '^ac_cv_env_' "${HERE}/substrate-cross.cache" > "${CACHE}"
echo "==> configure"
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --cache-file="${CACHE}" \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie \
            -Wno-error=implicit-function-declaration -Wno-error=implicit-int \
            -Wno-error=int-conversion -Wno-error=incompatible-pointer-types" \
    PTHREAD_CFLAGS="-D_REENTRANT" PTHREAD_LIBS="-lpthread" \
    LIBS="-lXmu -lXext -lXpm -lSM -lICE -liconv -lregex"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
# Plugins are built by all-local but not installed by the bin_PROGRAMS rule;
# stage them alongside the binary under a private dir.
if ls plugins/*.so >/dev/null 2>&1; then
    mkdir -p "${DESTDIR}/usr/lib/motifgpt/plugins"
    install -m 0755 plugins/*.so "${DESTDIR}/usr/lib/motifgpt/plugins/"
fi
echo "==> Done.  motifgpt staged at ${DESTDIR}/usr/bin/motifgpt"
