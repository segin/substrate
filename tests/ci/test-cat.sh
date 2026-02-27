#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

cd "$ROOT"

make -C tests/bin/cat clean
make -C tests/bin/cat test

if command -v clang >/dev/null 2>&1; then
    echo "Running cat sanitizer suite"
    make -C tests/bin/cat san
else
    echo "clang not found; skipping cat sanitizer suite"
fi

echo "Running cat fuzz smoke"
make -C tests/bin/cat fuzz-smoke
