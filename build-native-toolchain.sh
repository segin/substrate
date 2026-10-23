#!/bin/sh
#
# build-native-toolchain.sh — build substrate's own C compiler, assembler and
# linker (usr.bin/cc, usr.bin/as, usr.bin/ld, with usr.lib/elfobj) to run
# on the host OS (Linux), and optionally install them.
#
# These are the in-tree tools, built with NATIVE_BUILD=1 against the
# host's libc; contrib/build-toolchain.sh builds the GNU cross toolchain
# (binutils + GCC for i386-unknown-substrate) instead.
#
# The build runs in a copy of the sources under $BUILD_DIR, so it never
# leaves host objects in the tree where the target build would pick them
# up.
#
# Usage:
#   ./build-native-toolchain.sh            # build into $BUILD_DIR/bin
#   ./build-native-toolchain.sh --install  # ... and install under $PREFIX
#
# --install puts cc (and its cpp link), as and ld in $PREFIX/native/bin and
# cc's headers in $PREFIX/native/lib/substratecc/include, where cc looks
# for them.  Not in $PREFIX/bin: that holds only the *-unknown-substrate-*
# cross tools, and an unprefixed as or ld there is found by the host gcc
# and breaks host builds.  cc runs the as and ld beside it.
#
# Env (all optional):
#   PREFIX     install prefix                default /opt/substrate
#   BUILD_DIR  where the build is done       default build/native-toolchain
#   JOBS       -j parallelism                default `nproc`
#   HOST_CC    the host's C compiler         default cc
#
set -eu

TOP="$(cd "$(dirname "$0")" && pwd)"
: "${PREFIX:=/opt/substrate}"
: "${BUILD_DIR:=${TOP}/build/native-toolchain}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
: "${HOST_CC:=cc}"

install=0
for arg in "$@"; do
    case "${arg}" in
    --install) install=1 ;;
    -h|--help)
        sed -n '2,/^set -eu/{/^set -eu/d;s/^# \{0,1\}//;p;}' "$0"
        exit 0
        ;;
    *)
        echo "build-native-toolchain.sh: unknown argument ${arg} (try --help)" >&2
        exit 1
        ;;
    esac
done

case "$(uname -s)" in
Linux) ;;
*) echo "build-native-toolchain.sh: warning: only Linux hosts are tested" >&2 ;;
esac

SRC_DIR="${BUILD_DIR}/src"
BIN_DIR="${BUILD_DIR}/bin"

echo "==> Copying the sources to ${SRC_DIR}"
rm -rf "${SRC_DIR}" "${BIN_DIR}"
mkdir -p "${SRC_DIR}/usr.bin" "${SRC_DIR}/usr.lib" "${BIN_DIR}"
cp "${TOP}/Makefile.inc" "${TOP}/Makefile.bin.inc" "${SRC_DIR}/"
cp -R "${TOP}/include" "${SRC_DIR}/"
# the kernel's headers (libelfobj takes <exec/formats/elf.h> from sys/), not its sources
(cd "${TOP}" && find sys -name '*.h' | tar -cf - -T -) | tar -xf - -C "${SRC_DIR}"
for d in usr.bin/cc usr.bin/as usr.bin/ld usr.lib/elfobj; do
    cp -R "${TOP}/${d}" "${SRC_DIR}/${d}"
done

# the copies carry whatever the tree had built, perhaps for the target
for d in usr.lib/elfobj usr.bin/cc usr.bin/as usr.bin/ld; do
    make -s -C "${SRC_DIR}/${d}" NATIVE_BUILD=1 clean >/dev/null 2>&1 || true
done

for d in usr.lib/elfobj usr.bin/as usr.bin/ld usr.bin/cc; do
    echo "==> Building ${d}"
    make -C "${SRC_DIR}/${d}" -j"${JOBS}" NATIVE_BUILD=1 CC="${HOST_CC}"
done

cp "${SRC_DIR}/usr.bin/cc/cc" "${SRC_DIR}/usr.bin/as/as" "${SRC_DIR}/usr.bin/ld/ld" "${BIN_DIR}/"
ln -sf cc "${BIN_DIR}/cpp"

echo "==> Checking the tools"
probe="${BUILD_DIR}/probe"
mkdir -p "${probe}"
printf '#include <stdio.h>\nint main(void) { puts("build-native-toolchain: ok"); return 0; }\n' > "${probe}/hello.c"
"${BIN_DIR}/cc" -o "${probe}/hello" "${probe}/hello.c"
"${probe}/hello"
rm -rf "${probe}"

if [ "${install}" = 1 ]; then
    dest="${PREFIX}/native"
    echo "==> Installing in ${dest}"
    mkdir -p "${dest}/bin" "${dest}/lib/substratecc/include"
    for t in cc as ld; do
        install -m 755 "${BIN_DIR}/${t}" "${dest}/bin/${t}"
    done
    ln -sf cc "${dest}/bin/cpp"
    install -m 644 "${SRC_DIR}"/usr.bin/cc/resource/include/*.h "${dest}/lib/substratecc/include/"
    echo "==> ${dest}/bin: cc cpp as ld"
else
    echo "==> ${BIN_DIR}: cc cpp as ld (--install to install under ${PREFIX})"
fi
