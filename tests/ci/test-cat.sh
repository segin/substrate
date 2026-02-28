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

if command -v valgrind >/dev/null 2>&1; then
    echo "Running cat valgrind spot check"
    TMPDIR=$(mktemp -d)
    export TMPDIR
    trap 'rm -rf "$TMPDIR"' EXIT INT TERM
    python3 - <<'PY'
import pathlib
import random
import tempfile
import os

tmp = pathlib.Path(os.environ["TMPDIR"])
rng = random.Random(1337)
data = bytes(rng.getrandbits(8) for _ in range(1024 * 128))
(tmp / "in.bin").write_bytes(data)
PY
    valgrind --leak-check=full --error-exitcode=1 \
        "$ROOT/bin/cat/cat" "$TMPDIR/in.bin" >/dev/null
else
    echo "valgrind not found; skipping cat valgrind spot check"
fi
