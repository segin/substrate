#!/bin/sh
#
# contrib/sudo/build.sh — configure + build + install sudo for substrate.
# Produces:
#   /usr/bin/{sudo,sudoedit,sudoreplay,cvtsudoers}   (sudo is setuid root)
#   /usr/sbin/visudo
#   /usr/libexec/sudo/                               (plugins)
#   /etc/sudoers, /etc/sudoers.d/, /etc/sudo.conf
#   /usr/share/man/man{1,5,8}/
#
# Authentication is substrate's own: /etc/shadow through getspnam(3) and
# crypt(3), both in libc.  There is no PAM.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.9.17p2"
TREE_DIR="${HERE}/build/sudo-${VERSION}"
OBJ_DIR="${HERE}/build/obj"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-sudo}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

. "${HERE}/../substrate-autotools.sh"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
[ -e "${SR}/lib/libz.so" ] || { echo "zlib not staged -- build contrib/zlib first" >&2; exit 1; }

# The bundled libtool has to know substrate is an ELF/GNU-ld system, or the
# plugins (sudoers.so and friends, which sudo dlopen()s) come out static.
substrate_libtool_fix "${TREE_DIR}/configure"

rm -rf "${OBJ_DIR}"
mkdir -p "${OBJ_DIR}"
cd "${OBJ_DIR}"

# Probes configure would answer by running a test program.
#   - unsetenv() returns int, as POSIX has it; putenv() takes a plain char *.
#   - /dev/fd exists, so fexecve-style execution of a script by fd works.
export sudo_cv_func_unsetenv_void=no
export sudo_cv_func_putenv_const=no
export ac_cv_path_install="/usr/bin/install -c"

echo "==> configure"
# What is left out, and why:
#   PAM, LDAP, SSSD, Kerberos, SELinux, AppArmor, audit   not on substrate
#   log server/client, OpenSSL                            sudo_logsrvd is a
#                                                         network daemon nobody
#                                                         asked for
#   Python plugins, NLS                                   optional extras
#   sendmail                                              no MTA to hand mail to
#   noexec, intercept                                     both work by
#                                                         LD_PRELOADing a shim
#                                                         into the command
# --disable-shared-libutil and the system zlib: sudo would otherwise install
# libsudo_util.so and a private libz in /usr/libexec/sudo and find them
# through DT_RUNPATH, which substrate's ld.so does not implement.  configure
# only allows a static libutil together with --enable-static-sudoers, which
# builds the sudoers policy into sudo itself instead of a sudoers.so it
# dlopen()s -- one less thing a setuid program has to find at run time.
# --with-all-insults is not set: insults stay off unless sudoers asks.
# -lregex: regcomp(3) is in substrate's libregex, not in libc, and the
# libraries are linked with --no-undefined.  It rides in LDFLAGS because
# configure hands LIBS to the BUILD machine's compiler as well, for the
# generators it runs during the build, and the host has no libregex.  The
# library is shared, so its position ahead of the objects does not matter.
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --sysconfdir=/etc \
    --libexecdir=/usr/libexec \
    --mandir=/usr/share/man \
    --with-rundir=/var/run/sudo \
    --with-vardir=/var/db/sudo \
    --with-iologdir=/var/log/sudo-io \
    --with-logfac=auth \
    --with-editor=/bin/vi \
    --with-env-editor \
    --with-secure-path="/usr/sbin:/usr/bin:/sbin:/bin" \
    --with-passwd \
    --without-pam \
    --without-ldap \
    --without-sssd \
    --without-kerb5 \
    --without-selinux \
    --without-apparmor \
    --without-linux-audit \
    --without-bsm-audit \
    --without-sendmail \
    --with-noexec=no \
    --disable-intercept \
    --disable-log-server \
    --disable-log-client \
    --disable-openssl \
    --disable-python \
    --disable-nls \
    --enable-zlib=system \
    --enable-static-sudoers \
    --disable-shared-libutil \
    --disable-hardening \
    --disable-pie \
    --disable-tmpfiles.d \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    LDFLAGS="-fno-pie -lregex"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
# INSTALL_OWNER empty: the staging tree is written by an unprivileged build.
# Ownership (root) and sudo's setuid bit are set inside the image, by
# build-rootfs.sh, not on the host.
make install DESTDIR="${DESTDIR}" INSTALL_OWNER=""

# Brand what the dynamic linker loads, and drop the libtool archives.
for f in "${DESTDIR}"/usr/libexec/sudo/*.so* "${DESTDIR}"/usr/bin/* "${DESTDIR}"/usr/sbin/*; do
    [ -f "$f" ] && [ ! -L "$f" ] || continue
    magic=$(dd if="$f" bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$magic" = "7f454c46" ] || continue
    printf '\100' | dd of="$f" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
done
rm -f "${DESTDIR}"/usr/libexec/sudo/*.la

echo "==> Done.  sudo staged under ${DESTDIR}"
