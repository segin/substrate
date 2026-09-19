#!/bin/sh
# contrib/python/build.sh — cross-build CPython for substrate.
#
# Two stages, because a cross build of CPython needs an interpreter of the
# SAME major.minor to run its own build steps (freezing modules, compileall):
#
#   1. a build-host python from this very tarball, into build/host-prefix.
#      Built here rather than taken from the build machine so the port does
#      not depend on what the host happens to ship -- this machine has 3.14,
#      CI's runner has something else, and configure rejects a mismatch.
#   2. the substrate cross build, pointed at it with --with-build-python.
#
# config.site answers what a cross build cannot probe by running anything:
# substrate's devfs has /dev/ptmx (sys/drivers/console/pty.c) and no /dev/ptc.
#
# --enable-shared: extension modules (.so) resolve their Py* symbols from
# libpython3.14.so rather than from the executable's exported dynamic
# symbols.
#
# --disable-ipv6: substrate rejects AF_INET6 at socket() time on purpose
# (sys/net/af_inet.c), so there is no IPv6 to detect.  configure cannot run
# its getaddrinfo test under cross-compilation and assumes the resolver is
# broken, stopping with "You must get working getaddrinfo() function or pass
# the --disable-ipv6 option"; that is the honest answer here rather than
# telling it the resolver is fine with ac_cv_buggy_getaddrinfo=no.
#
# Modules deliberately off: readline (the sysroot has libedit but neither
# editline/readline.h nor libedit.pc, which is what CPython looks for), lzma
# and gdbm (no xz or gdbm port), tkinter (no Tk).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; PKG="python"; VERSION="3.14.7"; XY="3.14"
TREE="${HERE}/build/Python-${VERSION}"
HOSTPREFIX="${HERE}/build/host-prefix"; HOSTBUILD="${HERE}/build/host-build"
TGTBUILD="${HERE}/build/target-build"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${PKG}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -d "${TREE}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# CPython's bundled config.sub predates substrate and rejects the triple:
#     Invalid configuration 'i386-unknown-substrate': OS 'substrate' not recognized
# The helper prefers the copy in the binutils port and patches the tree's own
# when that is absent, which is every CI run that hits the toolchain cache.
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE}"

for h in ffi.h zlib.h bzlib.h openssl/ssl.h sqlite3.h expat.h curses.h uuid/uuid.h; do
    [ -f "${SR}/include/${h}" ] || { echo "build.sh: ${SR}/include/${h} missing -- build its port first" >&2; exit 1; }
done

# --- stage 1: the build-host interpreter -----------------------------------
if [ ! -x "${HOSTPREFIX}/bin/python${XY}" ]; then
    echo "==> stage 1: build-host python ${VERSION}"
    rm -rf "${HOSTBUILD}"; mkdir -p "${HOSTBUILD}"
    ( cd "${HOSTBUILD}" && "${TREE}/configure" --prefix="${HOSTPREFIX}" \
        --with-ensurepip=no --disable-test-modules \
        && make -j"${JOBS}" && make install )
fi
[ -x "${HOSTPREFIX}/bin/python${XY}" ] || { echo "build.sh: stage 1 produced no python${XY}" >&2; exit 1; }

# --- stage 2: the substrate cross build ------------------------------------
echo "==> stage 2: cross build for i386-unknown-substrate"
rm -rf "${TGTBUILD}"; mkdir -p "${TGTBUILD}"; cd "${TGTBUILD}"
cat > config.site <<SITE
ac_cv_file__dev_ptmx=yes
ac_cv_file__dev_ptc=no
SITE
export CONFIG_SITE="${TGTBUILD}/config.site"
export PKG_CONFIG_LIBDIR="${SR}/lib/pkgconfig"

"${TREE}/configure" \
    --host=i386-unknown-substrate --build="$(gcc -dumpmachine)" \
    --prefix=/usr \
    --enable-shared \
    --with-build-python="${HOSTPREFIX}/bin/python${XY}" \
    --with-system-expat \
    --with-openssl="${SR}" \
    --with-readline=no \
    --with-ensurepip=no \
    --disable-ipv6 \
    --disable-test-modules \
    CC=i386-unknown-substrate-gcc \
    CXX=i386-unknown-substrate-g++ \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    READELF=i386-unknown-substrate-readelf \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g" \
    CPPFLAGS="-I${SR}/include" \
    LDFLAGS="-L${SR}/lib -Wl,-rpath-link,${SR}/lib"

make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"

# Brand every ELF the install produced (interpreter, libpython, extension
# modules) with ELFOSABI_SUBSTRATE.
find "${DESTDIR}/usr" -type f | while IFS= read -r f; do
    [ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = '177ELF' ] || continue
    printf '\100' | dd of="$f" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
done
echo "==> ${PKG} staged under ${DESTDIR}"
