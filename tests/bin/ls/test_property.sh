#!/bin/sh
set -eu

LS_BIN=${1:-./ls_host}
TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/ls_prop.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

iter=0
while [ "$iter" -lt 30 ]; do
    dir="$WORK/d$iter"
    mkdir -p "$dir"

    python3 - "$dir" "$iter" <<'PY'
import os
import random
import string
import sys

root = sys.argv[1]
r = random.Random(int(sys.argv[2]))

alphabet = string.ascii_letters + string.digits + " _-.'\""
for i in range(80):
    name = "".join(r.choice(alphabet) for _ in range(r.randint(1, 40)))
    if name in (".", ".."):
        name += "_x"
    with open(os.path.join(root, name), "wb") as f:
        f.write(os.urandom(r.randint(0, 512)))

# control chars and utf8
open(os.path.join(root, "line\nbreak"), "wb").close()
open(os.path.join(root, "tab\tname"), "wb").close()
open(os.path.join(root, "e\u0301.txt"), "wb").close()
open(os.path.join(root, "中文.txt"), "wb").close()
open(os.path.join(root, "x" * 240), "wb").close()
PY

    "$LS_BIN" -1 --quoting-style=escape "$dir" >/dev/null
    "$LS_BIN" -l "$dir" >/dev/null
    "$LS_BIN" -C --width=24 "$dir" >/dev/null
    "$LS_BIN" -x --width=24 "$dir" >/dev/null
    "$LS_BIN" -m "$dir" >/dev/null
    "$LS_BIN" -v "$dir" >/dev/null
    "$LS_BIN" -X "$dir" >/dev/null
    "$LS_BIN" --ignore-case "$dir" >/dev/null

    iter=$((iter + 1))
done

echo "ls property: PASS"
