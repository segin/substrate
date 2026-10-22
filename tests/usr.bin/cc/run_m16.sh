#!/bin/sh
# -m16, as gcc has it: the i386 target, its code assembled under
# .code16gcc to run in real mode.  Linux builds its realmode trampoline
# and boot setup so; cc ignored the option and made x86-64 objects, which
# the i386 link then refused ("realmode/rm/header.o is for x86-64").
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

printf 'int f(int x) { return x + 1; }\n' > "$CC_TMP/m16.c"

"$CC_BIN" -m16 -S -o "$CC_TMP/m16.s" "$CC_TMP/m16.c"
grep -q '^\.code16gcc$' "$CC_TMP/m16.s"
! grep -q '^\.code32$' "$CC_TMP/m16.s"

"$CC_BIN" -m16 -c -o "$CC_TMP/m16.o" "$CC_TMP/m16.c"
# an ELF32 i386 object (class 1, machine 3)
[ "$(od -An -tu1 -j4 -N1 "$CC_TMP/m16.o" | tr -d ' ')" = 1 ]
[ "$(od -An -tu2 -j18 -N2 "$CC_TMP/m16.o" | tr -d ' ')" = 3 ]
# real-mode encoding: `push %ebp` carries an operand-size prefix
objcopy -O binary -j .text "$CC_TMP/m16.o" "$CC_TMP/m16.text"
[ "$(od -An -tx1 -N2 "$CC_TMP/m16.text" | tr -d ' ')" = 6655 ]

echo | "$CC_BIN" -m16 -dM -E - | grep -q '^#define __i386__ 1$'
! echo | "$CC_BIN" -m16 -dM -E - | grep -q '__x86_64__'
[ "$("$CC_BIN" -m16 -dumpmachine)" = "$("$CC_BIN" -m32 -dumpmachine)" ]

# -m32 is unchanged
"$CC_BIN" -m32 -S -o "$CC_TMP/m32.s" "$CC_TMP/m16.c"
grep -q '^\.code32$' "$CC_TMP/m32.s"
echo "run_m16: ok"
