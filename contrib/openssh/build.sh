#!/bin/sh
#
# build.sh — Configure + build + install OpenSSH portable for substrate.
#
# Per the user's directive: --without-pam and --without-X.  Builds
# both ssh client and sshd server.  Uses substrate's OpenSSL 3.x and
# zlib ports; resolved via PKG_CONFIG_PATH overlay onto the cross
# toolchain.
#
# Env overrides:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-openssh)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="10.0p2"
# Tarball extracts to openssh-10.0p1/ even on a p2 release; mirror.
TREE_DIR="${HERE}/build/openssh-10.0p1"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-openssh}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

TARGET="i386-unknown-substrate"
PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Stage paths for the contrib deps we link against.  OpenSSL + zlib
# pkg-config sit under their per-package DESTDIRs.
: "${OPENSSL_STAGE:=${SUBSTRATE_TOP}/dist-openssl}"
: "${ZLIB_STAGE:=${SUBSTRATE_TOP}/dist-zlib}"

# pkg-config search path so configure finds libssl/libcrypto/zlib.
PKGCFG=""
for stage in "${OPENSSL_STAGE}/usr/lib/pkgconfig" "${ZLIB_STAGE}/usr/lib/pkgconfig"; do
    [ -d "$stage" ] || continue
    PKGCFG="${PKGCFG}${PKGCFG:+:}${stage}"
done
[ -n "${PKGCFG}" ] && export PKG_CONFIG_PATH="${PKGCFG}"

# OpenSSH's configure links the test binaries with the real flags,
# so missing libsys transitive symbols (setsid, tcsetpgrp, ...) need
# the --copy-dt-needed-entries treatment same as inetutils.
export LDFLAGS="${LDFLAGS:-} -Wl,--copy-dt-needed-entries"

# Cross-compile cache hints — configure's runtime probes can't
# execute substrate binaries on the host, so feed it the answers
# for the ones that have only one sensible value on a POSIX system.
export ac_cv_func_strnlen_working=yes
export ac_cv_func_getpgrp_void=yes
export ac_cv_func_setpgrp_void=yes
export ac_cv_func_setvbuf_reversed=no
export ac_cv_func_memcmp_working=yes
export ac_cv_func_chown_works=yes

# Substrate's lib/resolv/ ships getrrsetbyname(); tell configure to
# skip the openbsd-compat fallback and link against -lresolv.
export ac_cv_func_getrrsetbyname=yes

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host="${TARGET}" \
    --prefix=/usr \
    --sysconfdir=/etc/ssh \
    --localstatedir=/var \
    --libexecdir=/usr/libexec \
    --with-pid-dir=/var/run \
    --with-privsep-path=/var/empty \
    --without-pam \
    --without-x \
    --without-selinux \
    --without-kerberos5 \
    --without-libedit \
    --with-zlib="${ZLIB_STAGE}/usr" \
    --with-ssl-dir="${OPENSSL_STAGE}/usr" \
    --with-default-path=/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin \
    --disable-strip \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
# OpenSSH's `install-nokeys` skips host-key generation (the keys are
# generated on first boot of the target, not at build time).
make install-nokeys DESTDIR="${DESTDIR}"

echo "==> Done.  Binaries staged under ${DESTDIR}/usr/{bin,sbin,libexec}"
