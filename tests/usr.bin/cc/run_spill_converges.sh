#!/bin/sh
# The register allocator takes a function whose inner loop makes calls with
# more values live across them than there are callee-saved registers.
#
# Each round spilled one value, and the rewrite gave the spilled value's
# references in the loop one shared register, which reached across the same
# calls as the value itself: so the next round had to spill another, a round
# at a time.  chain_spans() needed 13 rounds, one more than the allocator
# gives itself, and fell back ("not taken: allocation").  A shared register
# is now kept only where a register stays free for it.  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_spill_converges: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/cs.c" <<'EOF2'
struct link { struct link *next, *prev; };

struct item {
	unsigned long next;
	unsigned int size;
	struct link node;
	unsigned long tag;
};

struct batch { struct link items; unsigned long tag; };

struct pool { int kind; };

struct span {
	unsigned long base;
	unsigned long len;
};

struct batch *new_batch(struct pool *p);
struct item *take_item(struct pool *p);
int piece_size(struct pool *p, int len, int done);
void fill_item(struct pool *p, struct item *it, unsigned long base,
	       unsigned long done, unsigned long extra);

struct batch *chain_spans(struct pool *p, const struct span *sp,
			  unsigned long nsp)
{
	struct batch *b;
	struct item *it, *prev = 0;
	unsigned long done, n;
	unsigned long i;

	b = new_batch(p);
	if (!b)
		return 0;
	for (i = 0; i < nsp; i++) {
		done = 0;
		while (done < sp[i].len) {
			it = take_item(p);
			if (!it)
				goto fail;
			n = piece_size(p, sp[i].len, done);
			fill_item(p, it, sp[i].base, done, 0);
			it->size = n;
			if (prev)
				prev->next = it->tag;
			prev = it;
			done += n;
		}
	}
	return b;
fail:
	return 0;
}
EOF2
cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>

struct link { struct link *next, *prev; };
struct item {
	unsigned long next;
	unsigned int size;
	struct link node;
	unsigned long tag;
};
struct batch { struct link items; unsigned long tag; };
struct pool { int kind; };
struct span { unsigned long base; unsigned long len; };

struct batch *chain_spans(struct pool *p, const struct span *sp,
			  unsigned long nsp);

static struct batch the_batch;
static struct item items[64];
static int nitems, limit = 64;
static unsigned long sum;

struct batch *new_batch(struct pool *p) { return p->kind ? &the_batch : 0; }

struct item *take_item(struct pool *p)
{
	struct item *it;

	if (nitems >= limit)
		return 0;
	it = &items[nitems];
	it->tag = 1000 + nitems * 7 + p->kind;
	nitems++;
	return it;
}

int piece_size(struct pool *p, int len, int done)
{
	int n = (len - done) < 3 + p->kind ? len - done : 3 + p->kind;

	return n;
}

void fill_item(struct pool *p, struct item *it, unsigned long base,
	       unsigned long done, unsigned long extra)
{
	sum = sum * 31 + base * 3 + done + extra + (unsigned long)p->kind;
	it->next = 0;
}

int main(void)
{
	struct span sp[3] = { { 100, 7 }, { 200, 0 }, { 300, 11 } };
	struct pool p = { 1 }, none = { 0 };
	unsigned long chk = 0;
	struct batch *b;
	int i;

	b = chain_spans(&p, sp, 3);
	for (i = 0; i < nitems; ++i)
		chk = chk * 17 + items[i].next + items[i].size;
	printf("%d %d %lu %lu\n", b == &the_batch, nitems, sum % 1000003, chk % 1000003);
	nitems = 0;
	limit = 2;
	printf("%d %d\n", chain_spans(&p, sp, 3) == 0, chain_spans(&none, sp, 3) == 0);
	return 0;
}
EOF2
for m in -m64 -m32; do
	if ! printf 'int main(void) { return 0; }\n' | gcc $m -x c - -o "$CC_TMP/probe" 2>/dev/null; then
		echo "run_spill_converges: $m: skip (no gcc $m)"
		continue
	fi
	got=$(CC_MIR_STATS=2 "$CC_BIN" $m -O2 -c -o "$CC_TMP/cs.o" "$CC_TMP/cs.c" 2>&1)
	if [ -n "$got" ]; then
		echo "run_spill_converges: $m: [$got]"
		exit 1
	fi
	gcc $m -O1 -o "$CC_TMP/g" "$CC_TMP/cs.c" "$CC_TMP/m.c"
	want=$("$CC_TMP/g")
	gcc $m -O1 -c -o "$CC_TMP/m.o" "$CC_TMP/m.c"
	for o in O1 O2 Os; do
		"$CC_BIN" $m -$o -c -o "$CC_TMP/cs.o" "$CC_TMP/cs.c"
		gcc $m -o "$CC_TMP/c" "$CC_TMP/cs.o" "$CC_TMP/m.o"
		got=$("$CC_TMP/c") || { echo "run_spill_converges: $m -$o: the program fails"; exit 1; }
		if [ "$got" != "$want" ]; then
			echo "run_spill_converges: $m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
echo "run_spill_converges: ok"
