#!/bin/sh
# A static function or object nothing kept refers to is not emitted, as
# gcc drops it from -O1: transitively, and counting only the references
# left once inlining and dead code are gone.  A static const driver table
# whose address only an inlined helper's unread argument took stayed in
# the object, with the probe function its initializer names and that
# one's calls -- Linux's lm75 left i3c_device_match_id() undefined.  Kept,
# as gcc keeps them, are what has external linkage, what is marked used
# or a constructor, an asm operand, an alias's target, and what those
# name; a section attribute alone keeps nothing.  At -O0 gcc keeps every
# static but an unused static inline function, and cc does the same.
# The defined and undefined symbols are gcc's, -m64 and -m32, -O0 to -O2,
# with and without -g, and every function goes through MIR.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_drop_unreferenced: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/d.c" <<'EOF2'
extern int ext(void *);
extern int ext2(void);
struct drv { int (*probe)(void *); const char *name; };

/* the address only an inlined call's unread argument takes */
static int probe(void *p) { return ext(p); }
static const struct drv d = { .probe = probe, .name = "d" };
static inline int reg(const struct drv *x) { return 0; }
int init(void) { return reg(&d); }

/* a chain of statics nothing kept names */
static int chain_a = 1;
static int *chain_b = &chain_a;
static int **chain_c = &chain_b;
static int chain_fn(void) { return **chain_c; }

/* named by an external object */
static int via_ext = 5;
int *pub = &via_ext;

/* used, and what it names */
static int used_tgt = 2;
static int *used_obj __attribute__((used)) = &used_tgt;

/* a section of its own keeps nothing */
static int in_sec __attribute__((section(".data.cc_test"))) = 3;
static void sec_fn(void) __attribute__((section(".text.cc_test")));
static void sec_fn(void) { ext2(); }

/* an asm operand */
static int asm_obj = 7;
void asm_user(void) { __asm__ volatile("" :: "m"(asm_obj)); }

/* an alias's target */
static int al_tgt = 9;
extern int al_pub __attribute__((alias("al_tgt")));

/* a constructor, and what it names */
static int ctor_obj = 4;
static void ctor(void) __attribute__((constructor));
static void ctor(void) { ext(&ctor_obj); }

/* an address dead code takes */
static int dead_addr = 3;
void dead_user(void) { int *p = &dead_addr; (void)p; }

/* what only a dropped function calls */
static int dropped_obj = 6;
static int leaf(void) { return ext2() + dropped_obj; }
static int mid(void) { return leaf() + leaf(); }

/* a function a kept table holds */
static int kept_fn(void *p) { return ext(p) + 1; }
static const struct drv kd = { kept_fn, "kd" };
const struct drv *get_kd(void) { return &kd; }
EOF2

syms() {
	nm "$1" | awk '$NF !~ /^\./ { print ($(NF-1) == "U" ? "U" : "D"), $NF }' | sort -k2 -u
}
fail=0
for m in 64 32; do
	gcc -m$m -c -o "$CC_TMP/probe.o" "$CC_TMP/d.c" 2>/dev/null || continue
	for o in O0 O1 O2; do
		gcc -m$m -$o -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/d.c"
		want=$(syms "$CC_TMP/g.o")
		for g in "" -g; do
			what="-m$m -$o $g"
			if ! env CC_MIR_STATS=1 "$CC_BIN" -m$m -$o $g -c -o "$CC_TMP/c.o" "$CC_TMP/d.c" 2>"$CC_TMP/c.err"; then
				echo "run_drop_unreferenced: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
				exit 1
			fi
			if grep 'not taken' "$CC_TMP/c.err"; then
				echo "run_drop_unreferenced: $what: a function fell back from MIR"
				fail=1
			fi
			got=$(syms "$CC_TMP/c.o")
			if [ "$got" != "$want" ]; then
				echo "run_drop_unreferenced: $what defines and refers to"
				echo "$got" | sed 's/^/	cc:  /'
				echo "and gcc"
				echo "$want" | sed 's/^/	gcc: /'
				fail=1
			fi
		done
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_drop_unreferenced: ok"
