#!/bin/sh
# contrib/linux-kernel/build.sh — build Linux (i386, tinyconfig plus what
# booting to a userspace init needs) with substrate's own C compiler,
# assembler and linker, on a Linux host, and an initramfs whose /init is
# a static program built by the same tools.
#
#   ./fetch.sh && ./build.sh            -> build/out/bzImage, build/out/initramfs.cpio
#   tests/linux-kernel/boot.sh          boots them in qemu and checks /init ran
#
# Needs: the native tools (cc, as, ld: `make -C usr.bin/cc|as|ld
# NATIVE_BUILD=1`, installed to /opt/substrate/native/bin, or
# SUBSTRATE_NATIVE naming their directory); a host gcc for the kernel's
# own build helpers (fixdep, kconfig, ...), which run on the host; flex,
# bison, bc and perl, as any kernel build does.
#
# The configuration is `make tinyconfig` with substrate-boot.config merged
# over it: printk, the 8250 serial console, an initramfs (uncompressed)
# and ELF and script binaries.  olddefconfig is run with the same CC as
# the build, so kconfig's compiler probes agree and the build does not
# stop to ask about options they turn on.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="7.3"
TREE_DIR="${HERE}/build/linux-${VERSION}"
OUT_DIR="${HERE}/build/out"
: "${SUBSTRATE_NATIVE:=/opt/substrate/native/bin}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
: "${HOSTCC:=gcc}"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
for t in cc as ld; do
    [ -x "${SUBSTRATE_NATIVE}/${t}" ] || { echo "build.sh: no ${SUBSTRATE_NATIVE}/${t}" >&2; exit 1; }
done
# cc runs the as and ld beside it; the kernel's Makefiles may also run `as`/`ld` by name
PATH="${SUBSTRATE_NATIVE}:${PATH}"; export PATH

KMAKE="make -C ${TREE_DIR} ARCH=i386 CC=${SUBSTRATE_NATIVE}/cc LD=${SUBSTRATE_NATIVE}/ld HOSTCC=${HOSTCC}"

echo "==> Configuring (tinyconfig + substrate-boot.config)"
${KMAKE} -s tinyconfig >/dev/null
(cd "${TREE_DIR}" && scripts/kconfig/merge_config.sh -m .config "${HERE}/substrate-boot.config" >/dev/null)
${KMAKE} -s olddefconfig < /dev/null
for o in $(grep -o '^CONFIG_[A-Z0-9_]*=y' "${HERE}/substrate-boot.config"); do
    grep -q "^${o}\$" "${TREE_DIR}/.config" || { echo "build.sh: ${o} did not stay set" >&2; exit 1; }
done

echo "==> Building bzImage (-j${JOBS})"
${KMAKE} -j"${JOBS}" bzImage < /dev/null

echo "==> Building /init"
mkdir -p "${OUT_DIR}"
"${SUBSTRATE_NATIVE}/cc" -m32 -O0 -ffreestanding -fno-pic -nostdlib -static -Wl,-e,_start \
    -o "${OUT_DIR}/init" "${HERE}/init/init.c"

echo "==> Building the initramfs"
"${HOSTCC}" -O2 -o "${OUT_DIR}/gen_init_cpio" "${TREE_DIR}/usr/gen_init_cpio.c"
cat > "${OUT_DIR}/initramfs.list" <<EOF
dir /dev 755 0 0
nod /dev/console 600 0 0 c 5 1
file /init ${OUT_DIR}/init 755 0 0
EOF
"${OUT_DIR}/gen_init_cpio" "${OUT_DIR}/initramfs.list" > "${OUT_DIR}/initramfs.cpio"

cp "${TREE_DIR}/arch/x86/boot/bzImage" "${OUT_DIR}/bzImage"
echo "==> ${OUT_DIR}/bzImage and ${OUT_DIR}/initramfs.cpio"
