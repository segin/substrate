#!/bin/sh
# substrate-osabi-stamp.sh — brand ELF files with ELFOSABI_SUBSTRATE (0x40).
#
# The cross g++/gcc stamp ELFOSABI_SYSV(0) on their output.  Substrate's
# ld.so routes shared-object personality dispatch on the OSABI byte, so
# cross-built *.so must be branded 0x40 (plain OSABI-0 executables still run).
# Call this from a CMake port's build.sh after `make install`, e.g.:
#
#   ./substrate-osabi-stamp.sh "${DESTDIR}"        # whole install tree
#   ./substrate-osabi-stamp.sh "${DESTDIR}/usr/bin/foo"   # one file
#
# Stamps every ELF found (magic 0x7f 'E' 'L' 'F') under each argument.
set -eu

stamp_one() {
    f="$1"
    [ -f "$f" ] || return 0
    # Only touch real ELF files (first 4 bytes == \x7fELF).
    magic=$(dd if="$f" bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$magic" = "7f454c46" ] || return 0
    printf '\100' | dd of="$f" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
}

for arg in "$@"; do
    if [ -d "$arg" ]; then
        find "$arg" -type f | while IFS= read -r f; do stamp_one "$f"; done
    else
        stamp_one "$arg"
    fi
done
