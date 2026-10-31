#!/bin/sh
# run_x87_stack.sh [FIRST [LAST]]: x87 floating values stay on the x87
# stack within a block (mir_fp.c's stack cache).  A matrix multiply's
# inner loop keeps its products and sums on the stack: nothing in 80 bits
# goes through memory, and its only x87 accesses are the two elements
# loaded and the sum read and written (cc used to store every value to a
# frame slot of its own and load it back, twenty-four accesses, ten of
# them fstpt and fldt).  The programs gen_fp.py makes for seeds
# FIRST..LAST (default 1..30) -- more values live than the stack has
# registers, each read several times -- and a few roundings the cache
# leaves until they are needed must print what gcc's do, -m32 and -m64 at
# -O0 and -O2, every function through the allocator.  gcc is held to C's
# excess precision (-fexcess-precision=standard; GNU C's default lets it
# keep a double's excess precision past an assignment).  Needs a host gcc
# that builds -m32 programs; skipped, with a note, if not.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
GEN="$ROOT/tests/usr.bin/cc/gen_fp.py"
first=${1:-1}
last=${2:-30}
: "${AS:=as}"
: "${LD:=ld}"
export AS LD
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_x87_stack: skip (gcc cannot build -m32 programs)"
	exit 0
fi

cat >"$CC_TMP/mm.c" <<'EOF'
enum { N = 400 };
double a[N][N], b[N][N], c[N][N];

void mm(void) {
    int i, j, k;

    for (i = 0; i < N; ++i)
        for (j = 0; j < N; ++j) {
            double s = 0;

            for (k = 0; k < N; ++k)
                s += a[i][k] * b[k][j];
            c[i][j] = s;
        }
}
EOF
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/mm.s" "$CC_TMP/mm.c"
# the inner loop: the block, up to its first branch, that multiplies by b's elements (a row of a is found before it)
loop=$(awk '/^\.L/ { blk = "" } { blk = blk $0 "\n" } /^\tj[a-z]+ / { if (blk ~ /\$b/ && blk ~ /fmul/) { printf "%s", blk; exit } }' \
	"$CC_TMP/mm.s")
if [ -z "$loop" ]; then
	echo "run_x87_stack: no inner loop found in"
	cat "$CC_TMP/mm.s"
	exit 1
fi
wide=$(printf '%s' "$loop" | grep -cE '^	f(ld|stp)t ' || true)
mem=$(printf '%s' "$loop" | grep -cE '^	f[a-z]+ [^%]' || true)
if [ "$wide" -ne 0 ] || [ "$mem" -ne 4 ]; then
	echo "run_x87_stack: the inner loop has $wide 80-bit and $mem x87 memory accesses, not 0 and 4:"
	printf '%s' "$loop"
	exit 1
fi

# An n-body step's inner loop: dx, dy, dz, d2 and mag each rounded to a double once, through memory -- C's
# rounding at assignment, which gcc makes the same way -- and nothing else stored to the frame: no copy rounded
# again, no register spilled, no constant built there (they come from .rodata, a double made of two words' stores
# being read before they can be forwarded to the load).
cat >"$CC_TMP/nb.c" <<'EOF'
struct body { double x, y, z, vx, vy, vz, m; };

void step(struct body *b) {
    int i, j;

    for (i = 0; i < 5; ++i)
        for (j = i + 1; j < 5; ++j) {
            double dx = b[i].x - b[j].x, dy = b[i].y - b[j].y, dz = b[i].z - b[j].z;
            double d2 = dx * dx + dy * dy + dz * dz + 0.01;
            double mag = 0.001 / (d2 * d2);

            b[i].vx -= dx * b[j].m * mag;
            b[i].vy -= dy * b[j].m * mag;
            b[i].vz -= dz * b[j].m * mag;
            b[j].vx += dx * b[i].m * mag;
            b[j].vy += dy * b[i].m * mag;
            b[j].vz += dz * b[i].m * mag;
        }
}
EOF
"$CC_BIN" -m32 -O2 -S -o "$CC_TMP/nb.s" "$CC_TMP/nb.c"
loop=$(awk '/^\.L/ { blk = "" } { blk = blk $0 "\n" } /^\tj[a-z]+ / { if (blk ~ /fstpl [0-9]+\(%e/) { printf "%s", blk; exit } }' \
	"$CC_TMP/nb.s")
wide=$(printf '%s' "$loop" | grep -cE '^	f(ld|stp)t ' || true)
frame=$(printf '%s' "$loop" | grep -cE '^	(fst[a-z]* |mov[a-z]* [^,]*, )-?[0-9]+\(%[er][bs]p\)' || true)
if [ -z "$loop" ] || [ "$wide" -ne 0 ] || [ "$frame" -gt 5 ]; then
	echo "run_x87_stack: the n-body loop has $wide 80-bit accesses and $frame stores to the frame, not 0 and 5:"
	printf '%s' "${loop:-$(cat "$CC_TMP/nb.s")}"
	exit 1
fi

# roundings to a double or a float the cache defers, read back every way; loads it defers to their reader
cat >"$CC_TMP/round.c" <<'EOF'
#include <stdio.h>

static volatile long double in[4] = {1.0L / 3, 2.0L / 7, 1e-40L, 3.0L};
static float fs[2];
static double ds[2];
static long double ls[2];

static void __attribute__((noinline)) rounds(void) {
    long double x = in[0], y = in[1], z = in[2], w = in[3];
    double d = x * y;       /* rounded to a double: read in long double, compared, stored three ways */
    float f = x / w;        /* rounded to a float, a long double's excess dropped */
    float g = z * x;        /* a float subnormal */
    double e = (double)(x + y) + (float)(y * w);
    long double r = d + x;

    fs[0] = d;
    ds[0] = d;
    ls[0] = d;
    fs[1] = f + g;
    ds[1] = e * f;
    ls[1] = r - d;
    printf("%La %La %La %La %La %d %d\n", (long double)d, (long double)f, (long double)g, (long double)e, r,
           d == x * y, f < x / w);
}

static double mem[4] = {1.5, 2.5, 3.5, 4.5};
static double *volatile alias = mem;

/* a value loaded for one later read: the memory written, and the address's variable changed, before the read */
static void __attribute__((noinline)) loads(int n) {
    double *p = mem, *q = alias;
    double a = p[0], b = p[1], c = p[2];
    long double r;

    p[0] = 10;
    q[1] = 20;
    r = a * c + b;
    for (; n > 0; --n) {
        double x = *p;

        p = mem + n % 3;
        r = r * x + *p;
        *p = r - 1;
    }
    printf("%La %a %a %a\n", r, mem[0], mem[1], mem[2]);
}

int main(void) {
    rounds();
    loads(7);
    printf("%a %a %La %a %a %La\n", fs[0], ds[0], ls[0], fs[1], ds[1], ls[1]);
    return 0;
}
EOF

# check NAME SRC MODE: SRC built by gcc and cc, -mMODE at -O0 and -O2, prints the same
check() {
	name=$1 src=$2 mode=$3
	march=
	[ "$mode" = 32 ] && march=-march=i686
	# shellcheck disable=SC2086
	gcc -m$mode $march -O2 -fexcess-precision=standard -w -o "$CC_TMP/g" "$src"
	"$CC_TMP/g" >"$CC_TMP/want"
	for o in O0 O2; do
		CC_MIR_STATS=1 "$CC_BIN" -m$mode -$o -c -o "$CC_TMP/c.o" "$src" 2>"$CC_TMP/stats"
		if grep "^mir: .*not taken" "$CC_TMP/stats"; then
			echo "run_x87_stack: $name -m$mode -$o: the above do not go through the allocator"
			exit 1
		fi
		gcc -m$mode -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o"
		"$CC_TMP/c" >"$CC_TMP/got"
		if ! cmp -s "$CC_TMP/want" "$CC_TMP/got"; then
			echo "run_x87_stack: $name -m$mode -$o prints, where gcc's prints:"
			diff "$CC_TMP/got" "$CC_TMP/want" | sed -n '1,10p'
			exit 1
		fi
	done
}

for mode in 32 64; do
	check rounds "$CC_TMP/round.c" $mode
done
seed=$first
while [ "$seed" -le "$last" ]; do
	python3 "$GEN" "$seed" >"$CC_TMP/p.c"
	for mode in 32 64; do
		check "seed $seed" "$CC_TMP/p.c" $mode
	done
	seed=$((seed + 1))
done
echo "run_x87_stack: ok (seeds $first..$last)"
