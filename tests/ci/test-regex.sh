#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

cd "$ROOT"

make -C usr.lib/regex clean
make -C usr.lib/regex
make -C tests/usr.lib/regex clean
make -C tests/usr.lib/regex
make -C tests/usr.lib/regex run

# Optional sanitizers (best-effort)
if command -v clang >/dev/null 2>&1; then
    echo "Running ASAN build (best-effort)"
    make -C usr.lib/regex clean
    make -C usr.lib/regex CC=clang CFLAGS='-O1 -g -fsanitize=address -fno-omit-frame-pointer'
    make -C tests/usr.lib/regex clean
    make -C tests/usr.lib/regex CC=clang CFLAGS='-O1 -g -fsanitize=address -fno-omit-frame-pointer'
    make -C tests/usr.lib/regex run
fi

if command -v valgrind >/dev/null 2>&1; then
    echo "Running valgrind"
    valgrind --leak-check=full --error-exitcode=1 tests/usr.lib/regex/test_regex
fi
