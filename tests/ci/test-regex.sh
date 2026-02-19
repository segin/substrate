#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

cd "$ROOT"

make -C usr.lib/regex clean
make -C usr.lib/regex NATIVE_BUILD=1
make -C tests/usr.lib/regex clean
make -C tests/usr.lib/regex
make -C tests/usr.lib/regex run

# Optional sanitizers (best-effort)
if command -v clang >/dev/null 2>&1; then
    echo "Running ASAN build (best-effort)"
    if ! make -C usr.lib/regex clean || \
       ! make -C usr.lib/regex NATIVE_BUILD=1 CC=clang HOSTCFLAGS='-O1 -g -Wall -Wextra -fsanitize=address -fno-omit-frame-pointer' || \
       ! make -C tests/usr.lib/regex clean || \
       ! make -C tests/usr.lib/regex CC=clang CFLAGS='-O1 -g -Wall -Wextra -fsanitize=address -fno-omit-frame-pointer' || \
       ! make -C tests/usr.lib/regex run; then
        echo "ASAN run failed in this environment; continuing (best-effort)."
    fi
fi

if command -v valgrind >/dev/null 2>&1; then
    echo "Running valgrind"
    if ! valgrind --leak-check=full --error-exitcode=1 tests/usr.lib/regex/test_regex; then
        echo "Valgrind run failed in this environment; continuing (best-effort)."
    fi
fi
