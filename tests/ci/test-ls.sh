#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

make -C bin/ls clean
make -C bin/ls NATIVE_BUILD=1 ci

if command -v clang >/dev/null 2>&1; then
    echo "Running ls sanitizer build"
    make -C bin/ls clean
    make -C bin/ls NATIVE_BUILD=1 CC=clang TEST_CFLAGS='-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DTEST -DNATIVE_BUILD=1 -D_GNU_SOURCE -I.' test
fi

if command -v valgrind >/dev/null 2>&1; then
    echo "Running valgrind smoke"
    make -C bin/ls clean
    make -C bin/ls NATIVE_BUILD=1 ls_host
    valgrind --leak-check=full --error-exitcode=1 bin/ls/ls_host --help >/dev/null
fi

echo "ls CI: PASS"
