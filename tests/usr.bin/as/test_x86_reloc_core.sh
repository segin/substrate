#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-x86-reloc-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cc -Wall -Wextra -Werror -I"$ROOT/usr.bin/as" -iquote "$ROOT/include" \
   "$ROOT/usr.bin/as/as_x86_reloc.c" \
   "$ROOT/tests/usr.bin/as/test_x86_reloc_core.c" \
   "$ROOT/usr.lib/elfobj/libelfobj.a" \
   -o "$TMP/test_x86_reloc_core"

"$TMP/test_x86_reloc_core"
echo "ok: x86 reloc core"
