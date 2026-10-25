#!/bin/sh
# What inlining a function is reckoned to add to its caller's frame is
# the slots that stay memory -- those whose address escapes, and those
# larger than a word -- and a few spills: its values are the register
# allocator's.  A word was counted for every value, as the old emitters
# gave each a slot, and a static function called once with a few hundred
# values went over the frame limit and stayed a call: Linux's
# copy_present_ptes(), zap_present_ptes() and 84 more in mm/memory.c.
# A static function not declared inline whose body is no bigger than a
# call is inlined too, as gcc -O2 does.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_inline_frame_cost: skip (no gcc)"
	exit 0
fi

python3 - "$CC_TMP/f.c" <<'EOF2'
import sys
lines = ['#include <stdio.h>', '',
         'static long tiny(long a) { return a * 3 + 1; }', '',
         '/* called once: hundreds of values, no slot of its own */',
         'static long wide(const long *p, long k)', '{', '\tlong s = 0;']
for i in range(120):
    lines.append('\tlong t%d = p[%d] * (k + %d) ^ (s >> %d);' % (i, i % 16, i, i % 7))
    lines.append('\ts += t%d + (t%d >> 3);' % (i, i))
lines += ['\treturn s;', '}', '',
          'long __attribute__((noinline)) run(const long *p, long k)', '{',
          '\treturn wide(p, k) + tiny(k) + tiny(k + 1);', '}', '',
          'int main(void)', '{', '\tlong p[16];', '\tint i;', '',
          '\tfor (i = 0; i < 16; ++i)', '\t\tp[i] = i * 7 - 40;',
          '\tprintf("%ld\\n", run(p, 5));', '\treturn 0;', '}']
open(sys.argv[1], 'w').write('\n'.join(lines) + '\n')
EOF2
gcc -O1 -o "$CC_TMP/g" "$CC_TMP/f.c"
want=$("$CC_TMP/g")
"$CC_BIN" -m64 -O2 -o "$CC_TMP/c" "$CC_TMP/f.c"
got=$("$CC_TMP/c")
if [ "$got" != "$want" ]; then
	echo "run_inline_frame_cost: prints [$got], gcc's [$want]"
	exit 1
fi
"$CC_BIN" -m64 -O2 -S -o "$CC_TMP/f.s" "$CC_TMP/f.c"
if grep -E 'call[q]? +(wide|tiny)$' "$CC_TMP/f.s"; then
	echo "run_inline_frame_cost: the calls above are not inlined"
	exit 1
fi
echo "run_inline_frame_cost: ok"
