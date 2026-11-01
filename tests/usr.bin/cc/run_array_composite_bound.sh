#!/bin/sh
# A file-scope array defined without a bound takes the bound an earlier
# declaration gives it: the composite type (C11 6.2.7p3), the
# initializer filling what it fills.  cc gave the definition the bound
# of its initializer and called the two conflicting -- Linux's
# `extern const struct nla_policy ethnl_rings_set_policy[MAX + 1];` and
# its definition `[] = { [A] = ..., [B] = ... }`, B short of MAX
# (net/ethtool/rings.c, the Arch config).  Sizes, contents and the
# symbols' sizes are gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v nm >/dev/null 2>&1; then
	echo "run_array_composite_bound: skip (no gcc or nm)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/a.c" <<'EOF2'
#include <stdio.h>

enum { A, B, C, MAX };
struct pol {
	int type;
	int len;
};

extern const struct pol policy[MAX + 1];
const struct pol policy[] = {
	[A] = { 1, 2 },
	[C] = { 3, 4 },
};

extern int listed[6];
int listed[] = { 1, 2, 3 };

extern char text[8];
char text[] = "abc";

int main(void)
{
	printf("%zu %zu %zu %d %d %d %d %s %d\n", sizeof(policy), sizeof(listed), sizeof(text), policy[C].len,
	       policy[MAX].type, listed[2], listed[5], text, text[7]);
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/a.c" 2>/dev/null || continue
	gcc -m$m -O1 -c -o "$CC_TMP/g.o" "$CC_TMP/a.c"
	want=$("$CC_TMP/g")
	want_sizes=$(nm -S "$CC_TMP/g.o" | awk '$4 == "policy" || $4 == "listed" || $4 == "text" {print $4, $2}' | sort)
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/a.o" "$CC_TMP/a.c" 2>"$CC_TMP/c.err"; then
			echo "run_array_composite_bound: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/a.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_array_composite_bound: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
		got_sizes=$(nm -S "$CC_TMP/a.o" | awk '$4 == "policy" || $4 == "listed" || $4 == "text" {print $4, $2}' | sort)
		if [ "$got_sizes" != "$want_sizes" ]; then
			echo "run_array_composite_bound: -m$m -$o symbol sizes [$got_sizes], gcc's [$want_sizes]"
			exit 1
		fi
	done
done
echo "run_array_composite_bound: ok"
