#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

make -C bin/cp clean
make -C bin/cp NATIVE_BUILD=1 ci

# Sanitizer runs (best-effort when toolchain supports them)
if command -v clang >/dev/null 2>&1; then
    echo "Running cp sanitizer build"
    make -C bin/cp clean
    make -C bin/cp NATIVE_BUILD=1 CC=clang TEST_CFLAGS='-O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -DCP_HOST_BUILD -D_GNU_SOURCE -I.' unit integration property stress
fi

if command -v valgrind >/dev/null 2>&1; then
    echo "Running valgrind smoke"
    make -C bin/cp clean
    make -C bin/cp NATIVE_BUILD=1 cp_host
    valgrind --leak-check=full --error-exitcode=1 bin/cp/cp_host --help >/dev/null
fi

echo "cp CI: PASS"
