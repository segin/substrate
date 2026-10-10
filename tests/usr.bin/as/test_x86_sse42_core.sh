#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-x86-sse42-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_x86_sse42.c" \
   "$ROOT/tests/usr.bin/as/test_x86_sse42_core.c" \
   -o "$TMP/test_x86_sse42_core"

"$TMP/test_x86_sse42_core"
echo "ok: x86 sse4.2 core"
