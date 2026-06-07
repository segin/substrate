#!/bin/sh
#
# contrib/mksh/build.sh — cross-build the MirBSD Korn Shell for substrate.
#
# mksh has no autotools; its Build.sh probes the compiler (mostly
# compile-only tests, which cross-compile fine) and emits a build.  We point
# it at the substrate cross toolchain, force TARGET_OS=Linux (substrate is
# ELF/Linux-shaped and there is no mksh OS profile for it), and link it as a
# PIE against libc.so.0 + libsys.so.0 through /sbin/ld.so — the same shape as
# substrate's in-tree bin/ programs.  mksh's default -fstack-protector-strong
# is kept: it links now that libc ships __stack_chk_guard and crt0 the
# __stack_chk_fail_local PIC helper.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-mksh)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
TREE_DIR="${HERE}/build/mksh"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-mksh}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

GCCVER=$(ls "${STAGE1_PREFIX}/lib/gcc/i386-unknown-substrate/" | head -1)
LIBGCC="${STAGE1_PREFIX}/lib/gcc/i386-unknown-substrate/${GCCVER}/libgcc.a"
CRT0="${STAGE1_PREFIX}/i386-unknown-substrate/lib/crt0.o"

export CC=i386-unknown-substrate-gcc
export TARGET_OS=Linux
export CFLAGS="-march=i486 -mtune=i486 -O2 -fPIE"
export LDFLAGS="-nostdlib -pie -fPIE \
  -Wl,--dynamic-linker=/sbin/ld.so \
  -Wl,--unresolved-symbols=ignore-in-shared-libs \
  ${CRT0} \
  -L${SUBSTRATE_TOP}/lib/c -l:libc.so.0 \
  -L${SUBSTRATE_TOP}/lib/sys -l:libsys.so.0 \
  -Wl,-rpath-link,${SUBSTRATE_TOP}/lib/c -Wl,-rpath-link,${SUBSTRATE_TOP}/lib/sys \
  ${LIBGCC}"

cd "${TREE_DIR}"
rm -f mksh *.o
echo "==> Build.sh"
sh Build.sh -Q

[ -f mksh ] || { echo "build.sh: mksh did not build" >&2; exit 1; }

# Brand the native OSABI byte ELFOSABI_SUBSTRATE (0x40) so the kernel's exec
# personality dispatch routes it to the native loader.
printf '\100' | dd of=mksh bs=1 seek=7 count=1 conv=notrunc status=none

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}/bin" "${DESTDIR}/etc"
cp mksh "${DESTDIR}/bin/mksh"
chmod 0755 "${DESTDIR}/bin/mksh"
# /bin/ksh is what CDE's configure and scripts look for.
ln -sf mksh "${DESTDIR}/bin/ksh"
# Ship the sample interactive rc as a reference.
[ -f dot.mkshrc ] && cp dot.mkshrc "${DESTDIR}/etc/mkshrc"

echo "==> Done.  mksh staged under ${DESTDIR} (/bin/mksh, /bin/ksh -> mksh)"
