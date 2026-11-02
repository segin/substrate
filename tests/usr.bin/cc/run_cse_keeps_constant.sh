#!/bin/sh
# A constant stays a constant through common subexpressions: within a
# block, cc made a constant a copy of a variable that held the same value
# there.  Right, but the constant was lost to what needs one: Linux's
# static branch, `"i"(branch)` of arch_static_branch() inlined into
# iavf_clean_rx_irq()'s loop -- its false hoisted beside `bool failure =
# false` -- was refused ("impossible constraint in 'asm'"): iavf_txrx.c,
# ixgbe_xsk.c.  A loop of the same shape compiles, -m64 and -m32, -O0, -O1
# and -O2, on both code paths, and its jump-table entries are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_cse_keeps_constant: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
export AS

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdbool.h>
struct key { int enabled; };
struct tp { const char *name; struct key key; };
struct tp __tracepoint_clean = { "clean", { 0 } };
struct ring { unsigned short next, count; unsigned long *desc; void *skb; };
struct info { unsigned int len; unsigned short ptype; };
int sink;
bool alloc_bufs(struct ring *r, unsigned short n);
void trace_body(struct ring *r, unsigned long *d, void *skb);
static inline __attribute__((always_inline)) bool sb(struct key *key, bool branch)
{
	asm goto("1: nop\n\t.pushsection .discard.jl, \"aw\"\n\t.quad 1b, %c0 + %c1\n\t.popsection"
		 : : "i"(key), "i"(branch) : : l_yes);
	return false;
l_yes:
	return true;
}
static inline __attribute__((always_inline)) void trace_clean(struct ring *r, unsigned long *d, void *skb)
{
	if (sb(&__tracepoint_clean.key, false))
		trace_body(r, d, skb);
}
static inline struct info extract(struct ring *r, unsigned long *d)
{
	struct info i = { (unsigned int)(*d >> 32), (unsigned short)(*d & 0xff) };
	(void)r;
	return i;
}
int clean(struct ring *r, int budget)
{
	unsigned int total = 0;
	void *skb = r->skb;
	unsigned short cleaned = r->count;
	bool failure = false;

	while (total < (unsigned int)budget) {
		struct info fields;
		unsigned long *d;

		if (cleaned >= 16) {
			failure = failure || alloc_bufs(r, cleaned);
			cleaned = 0;
		}
		d = &r->desc[r->next];
		if (!(*d & 1))
			break;
		fields = extract(r, d);
		trace_clean(r, d, skb);
		total += fields.len;
		cleaned++;
		r->next++;
	}
	r->skb = skb;
	return failure ? budget : (int)total;
}
EOF2
entries() { readelf -rW "$1" | awk '/Relocation section .*discard/ { p = 1; next } /Relocation section/ { p = 0 } p && /R_/ { print $5, $NF }' | sort | uniq -c; }
for m in 64 32; do
	gcc -m$m -O2 -c -o "$CC_TMP/g.o" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$(entries "$CC_TMP/g.o" | awk '{ print $2 }' | sort -u)
	for path in mir legacy; do
		run="env"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		for o in O0 O1 O2; do
			if ! $run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/c.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
				echo "run_cse_keeps_constant: -m$m $path -$o refused: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			got=$(entries "$CC_TMP/c.o" | awk '{ print $2 }' | sort -u)
			if [ "$got" != "$want" ]; then
				echo "run_cse_keeps_constant: -m$m $path -$o table names [$got], gcc's [$want]"
				exit 1
			fi
		done
	done
done
echo "run_cse_keeps_constant: ok"
