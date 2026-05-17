#!/bin/sh
#
# build.sh — configure + build + install inetutils for substrate.
#
# Builds the cross way: host = Linux, target = substrate.  Uses the
# stage-1 substrate toolchain at $STAGE1_PREFIX (default /opt/substrate-
# toolchain).  Disables encryption and most of the suite — see
# README.SUBSTRATE.md.
#
# Env overrides:
#   SUBSTRATE_TOP    repo root (autodetected by walking up)
#   STAGE1_PREFIX    toolchain prefix (default /opt/substrate)
#   DESTDIR          staging dir for `make install`
#                    (default ${SUBSTRATE_TOP}/dist-inetutils)
#   JOBS             parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.5"
TREE_DIR="${HERE}/build/inetutils-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-inetutils}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

TARGET="i386-unknown-substrate"
PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

if [ ! -d "${TREE_DIR}" ]; then
    echo "build.sh: source tree missing — run ./fetch.sh first" >&2
    exit 1
fi

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "==> configure"
# Encryption + authentication are enabled now that contrib/openssl
# is part of the build.  Substrate doesn't ship Kerberos yet
# (--without-krb5), so the krb-based AUTH paths will be no-ops at
# runtime, but the support is compiled in and ready for a future
# contrib/krb5 to light it up.  OpenSSL is autodetected by the
# configure script when its pkg-config metadata is in PKG_CONFIG_PATH;
# we point pkg-config at substrate's staged openssl tree.
: "${OPENSSL_STAGE:=${SUBSTRATE_TOP}/dist-openssl}"
if [ -d "${OPENSSL_STAGE}/usr/lib/pkgconfig" ]; then
    export PKG_CONFIG_PATH="${OPENSSL_STAGE}/usr/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
fi

# Linker flag rationale: substrate's libsys.so.0 supplies wrappers
# like setsid(), tcsetpgrp(), etc., and libc.so.0 lists it as
# DT_NEEDED.  Modern binutils defaults to --no-copy-dt-needed-entries,
# so calls to setsid() in caller code (e.g. inetd.c) don't resolve
# through libc -> libsys; the link fails with "DSO missing from
# command line".  Re-enable the historical pass-through.
export LDFLAGS="${LDFLAGS:-} -Wl,--copy-dt-needed-entries"

"${TREE_DIR}/configure" \
    --host="${TARGET}" \
    --prefix=/usr \
    --disable-shared --enable-static \
    --without-krb5 \
    --enable-authentication \
    --enable-encryption \
    --disable-ftp --disable-ftpd \
    --disable-rcp --disable-rsh --disable-rshd \
    --disable-rlogin --disable-rlogind \
    --disable-talk --disable-talkd \
    --disable-tftp --disable-tftpd \
    --disable-uucpd \
    --enable-inetd \
    --disable-syslogd \
    --disable-traceroute \
    --disable-ping --disable-ping6 \
    --disable-hostname --disable-dnsdomainname \
    --disable-logger --disable-whois \
    --disable-ifconfig \
    --disable-rexec --disable-rexecd \
    --enable-telnet --enable-telnetd

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  Binaries staged under ${DESTDIR}/usr/bin and /usr/sbin"
