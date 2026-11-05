#!/bin/sh
# CC_MIR_STATS=2 prints only the functions the register allocator does not
# take, the fallbacks; 1 prints every function, taken or not; unset, 0 or
# anything else prints nothing.
#
# There was only 1: a Linux build printed some sixty thousand "taken"
# lines, every static inline of every header once for each file, around
# the handful of fallbacks that are the point.
#
# chain_spans() falls back ("not taken: allocation"): the linear-scan
# allocator spills one interval a round around the calls of its inner loop
# and needs one round more than it gives itself.  plain() is taken.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/fb.c" <<'EOF2'
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

int plain(int a) { return a + 1; }
EOF2

# the stats lines of a compile with CC_MIR_STATS set to $1 (unset if "-")
stats() {
	if [ "$1" = - ]; then
		"$CC_BIN" -m64 -O2 -c -o "$CC_TMP/fb.o" "$CC_TMP/fb.c" 2>&1 | grep '^mir:' || true
	else
		CC_MIR_STATS=$1 "$CC_BIN" -m64 -O2 -c -o "$CC_TMP/fb.o" "$CC_TMP/fb.c" 2>&1 | grep '^mir:' || true
	fi
}

fallback='mir: chain_spans: not taken: allocation'
got=$(stats 2)
if [ "$got" != "$fallback" ]; then
	echo "run_mir_stats_fallbacks: CC_MIR_STATS=2 printed [$got], not only [$fallback]"
	exit 1
fi
got=$(stats 1)
if ! printf '%s\n' "$got" | grep -qx "$fallback" || ! printf '%s\n' "$got" | grep -qx 'mir: plain: taken'; then
	echo "run_mir_stats_fallbacks: CC_MIR_STATS=1 printed [$got], not both functions"
	exit 1
fi
for v in - 0 x; do
	got=$(stats $v)
	if [ -n "$got" ]; then
		echo "run_mir_stats_fallbacks: CC_MIR_STATS=$v printed [$got]"
		exit 1
	fi
done
echo "run_mir_stats_fallbacks: ok"
