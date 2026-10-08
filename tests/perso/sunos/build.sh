#!/bin/sh
#
# build.sh - build suntest, a Sun386i COFF program, with the host's tools.
#
#     tests/perso/sunos/build.sh [OUTPUT]
#
# Needs a C compiler that makes 32-bit x86 code (cc -m32), its nm and
# objcopy, and python3.  Needs nothing of SunOS's.  OUTPUT defaults to
# suntest beside this script.  README.md says how to run the result.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-${HERE}/suntest}"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

${CC:-cc} -m32 -O1 -Wall -ffreestanding -fno-pie -fno-stack-protector \
    -fno-asynchronous-unwind-tables -fno-builtin \
    -c -o "${TMP}/suntest.o" "${HERE}/suntest.c"
${CC:-cc} -m32 -nostdlib -static -no-pie -Wl,--build-id=none \
    -Wl,-T,"${HERE}/sun386.ld" -o "${TMP}/suntest.elf" "${TMP}/suntest.o"
${NM:-nm} "${TMP}/suntest.elf" > "${TMP}/suntest.nm"
${OBJCOPY:-objcopy} -O binary "${TMP}/suntest.elf" "${TMP}/suntest.bin"
python3 "${HERE}/mkcoff.py" "${TMP}/suntest.bin" "${TMP}/suntest.nm" "${OUT}"
chmod +x "${OUT}"
echo "built ${OUT}"
