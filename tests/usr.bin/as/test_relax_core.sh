#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-relax-core-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

{
  echo "start:"
  echo "jmp target"
  i=0
  while [ "$i" -lt 220 ]; do
    echo "nop"
    i=$((i+1))
  done
  echo "target:"
  echo "nop"
} > "$TMP/x86.s"

{
  echo "b target"
  i=0
  while [ "$i" -lt 40 ]; do
    echo "nop"
    i=$((i+1))
  done
  echo "target:"
  echo "nop"
} > "$TMP/arm.s"

cc -Wall -Wextra -Werror -D_GNU_SOURCE -I"$ROOT/usr.bin/as" \
   "$ROOT/usr.bin/as/as_lexer.c" "$ROOT/usr.bin/as/as_parser.c" "$ROOT/usr.bin/as/as_relax.c" \
   "$ROOT/tests/usr.bin/as/test_relax_core.c" \
   -o "$TMP/test_relax_core"

"$TMP/test_relax_core" "$TMP/x86.s" "$TMP/arm.s"
echo "ok: relax core"
