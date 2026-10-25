#!/bin/sh
# After allocation a spill slot is read from the register that still
# holds what was last stored to it or loaded from it, within a block: the
# reload becomes a copy, or goes.  Linear scan spills a value for the
# whole of its life, and its every read was a load, though the value had
# been stored from a register an instruction before.  Results are gcc's;
# no store to a frame slot is read back by the next instruction.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_spill_reload_forward: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/p.c" <<'EOF2'
#include <stdio.h>

long __attribute__((noinline)) ext(long x) { return x * 3 + 1; }

/* more values live across each call than there are callee-saved registers */
long __attribute__((noinline)) pressure(long a, long b, long c, long d, long e, long f)
{
	long g = a * b, h = c * d, i = e * f, j = a + f, k = b + e, l = c + d, m = a ^ c;
	long r = ext(g);

	r += ext(h) + g;
	r += ext(i) + h + j;
	r += ext(j) + i + k + l;
	r += ext(k) + l + m + g;
	r += ext(l) + m + h + i + j;
	return r + k + m;
}

int main(void)
{
	printf("%ld\n", pressure(1, 2, 3, 4, 5, 6));
	printf("%ld\n", pressure(-7, 11, 13, -17, 19, 23));
	return 0;
}
EOF2
# which register holds each frame slot, as far as one block goes: a label, a jump or a call forgets,
# and an instruction writing a register (its last operand) forgets what it held
cat >"$CC_TMP/held.awk" <<'EOF2'
function reg(r) { sub(/^%/, "", r); if (r ~ /^r[0-9]+[dwb]?$/) { sub(/[dwb]$/, "", r); return r }
	sub(/^[re]/, "", r); sub(/[lhx]$/, "", r); return r }
/^[.A-Za-z_0-9]+:/ || /^\tj/ || /^\tcall/ || /^\tret/ { delete held; next }
/^\t/ {
	line = $0; sub(/^\t[a-z0-9]+ */, "", line); n = split(line, op, ", ")
	if ($1 ~ /^mov[lq]$/ && n == 2 && op[1] ~ /^%/ && op[2] ~ /\(%[er]bp\)$/) { held[op[2]] = reg(op[1]); next }
	if ($1 ~ /^mov[lq]$/ && n == 2 && op[1] ~ /\(%[er]bp\)$/ && op[2] ~ /^%/) {
		if (op[1] in held) { print; bad = 1 }
		d = reg(op[2]); for (k in held) if (held[k] == d) delete held[k]
		held[op[1]] = d; next
	}
	if ($1 ~ /^(cmp|test|push)/) next
	if (n >= 1 && op[n] ~ /^%/) { d = reg(op[n]); for (k in held) if (held[k] == d) delete held[k] }
	if (n >= 1 && op[n] ~ /\(%[er]bp\)$/) delete held[op[n]]
	if ($1 ~ /^(cltd|cqto|cltq|div|idiv|mul)/) delete held
}
END { exit !bad }
EOF2
modes=64
if printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	modes="64 32"
fi
for m in $modes; do
	gcc -m$m -O1 -o "$CC_TMP/pg" "$CC_TMP/p.c"
	want=$("$CC_TMP/pg")
	"$CC_BIN" -m$m -O2 -o "$CC_TMP/pc" "$CC_TMP/p.c"
	got=$("$CC_TMP/pc")
	if [ "$got" != "$want" ]; then
		echo "run_spill_reload_forward: -m$m prints [$got], gcc's [$want]"
		exit 1
	fi
	"$CC_BIN" -m$m -O2 -S -o "$CC_TMP/p.s" "$CC_TMP/p.c"
	# a slot read while a register still holds what was stored to it, within a block
	if awk -f "$CC_TMP/held.awk" "$CC_TMP/p.s"; then
		echo "run_spill_reload_forward: -m$m: a slot is read back from memory while a register holds it"
		exit 1
	fi
done
echo "run_spill_reload_forward: ok"
