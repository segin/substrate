#!/bin/sh
# A static function called from one place only is inlined there from -O1,
# and its own copy dropped, as gcc's -finline-functions-called-once does.
# Linux depends on it: head32.c's zap_early_initrd_mapping() reads
# __initdata and is called only from __init code, and modpost rejects
# the reference from a copy cc left in .text.  A function called twice,
# whose address is taken, marked noinline, or on a cycle of calls keeps
# its copy -- inlined into the cycle, its frame would grow every level of
# the recursion, which ran cc's own lower_expr() out of stack -- and so
# does one whose locals would grow its caller's frame past gcc's limit
# (over 256 bytes and by more than 1000%) or past 1 KiB, as a Linux
# kernel stack is 8 KiB; at -O0 nothing is inlined.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/once.c" <<'EOF'
static int table[4] __attribute__((section(".init.data"))) = {1, 2, 3, 4};

static void once(int *out) {
	int i;

	for (i = 0; i < 4; ++i) {
		out[i] = table[i] * 3;
	}
}

static int twice(int x) {
	/* (more than a call's worth, as down()) */
	return x * 7 + table[x & 3] * (x ^ 11) - table[(x >> 2) & 3] + (x | 3) * table[(x + 1) & 3];
}

static int taken(int x) {
	return x - table[0];
}

static __attribute__((noinline)) int barred(int x) {
	return x + table[1];
}

int (*fp)(int) = taken;

static int down(int n);

int up(int n) {
	return n > 0 ? down(n - 1) + 1 : 0;
}

static int down(int n) {
	/* (more than a call's worth: a small static function is inlined whatever calls it, as gcc does) */
	return n > 0 ? up(n - 1) * 2 + (n ^ 5) * 3 - (n >> 2) + table[n & 3] * (n | 9) : 0;
}

static int bigframe(int x) {
	volatile char buf[4096];

	buf[x & 4095] = (char)x;
	return buf[0];
}

static int capped(int x) {
	volatile char buf[600];

	buf[x % 600] = (char)x;
	return buf[1];
}

int wide(int x) {
	volatile char own[600];

	own[x % 600] = (char)x;
	return own[2] + capped(x);
}

__attribute__((section(".init.text"))) int setup(int *out) {
	once(out);
	return twice(out[0]) + twice(out[1]) + barred(out[2]) + bigframe(out[3]);
}
EOF
status=0
check() { # label opt m expect-present...
	label=$1 opt=$2 m=$3
	shift 3
	if ! "$CC_BIN" -m$m $opt -S -o "$CC_TMP/once.s" "$CC_TMP/once.c" 2> "$CC_TMP/once.err"; then
		echo "FAIL $label: $(head -1 "$CC_TMP/once.err")"
		status=1
		return
	fi
	for want in "$@"; do
		name=${want#!}
		if grep -q "^$name:" "$CC_TMP/once.s"; then
			present=1
		else
			present=0
		fi
		if [ "$want" != "$name" ] && [ "$present" = 1 ]; then
			echo "FAIL $label: $name is still emitted"
			status=1
		elif [ "$want" = "$name" ] && [ "$present" = 0 ]; then
			echo "FAIL $label: $name is missing"
			status=1
		fi
	done
}
for m in 32 64; do
	check "-O2 -m$m" -O2 $m '!once' twice taken barred down bigframe capped setup
	check "-O1 -m$m" -O1 $m '!once' twice taken barred down bigframe capped setup
	check "-O0 -m$m" -O0 $m once twice taken barred down bigframe capped setup
done
[ "$status" -eq 0 ] && echo "run_inline_called_once: ok"
exit "$status"
