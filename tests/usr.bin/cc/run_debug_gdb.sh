#!/bin/sh
# A program built with cc -g debugs in gdb as one gcc -g builds: at a
# breakpoint, the backtrace names each function with its arguments and
# its file and line, `info locals` and `print` show the locals, the
# parameters, the statics and the globals, a structure's members through
# a pointer, and ptype the types as declared -- typedef names,
# enumerations, structures.  cc's -g gave gdb a line table of one row per
# function (line 1, after the prologue) and no .debug_info at all: gdb
# knew no variable, no type and no line, and a breakpoint on a function
# stopped before its parameters were stored.  gcc is the judge: the same
# gdb commands on cc's program and gcc's print the same, addresses
# aside.  -O0, -m64 and -m32, both code paths.  At -O2 a variable kept in
# memory still prints its value, and one kept in a register prints as
# optimized out -- never a stale slot's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v gdb >/dev/null 2>&1; then
	echo "run_debug_gdb: skip (no gcc or gdb)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/p.c" <<'EOF2'
typedef unsigned int u32;
struct point { int x, y; const char *name; };
enum mode { OFF, SLOW = 2, FAST };
struct point gp = { 3, 4, "gp" };
static int counter = 7;
static const char *names[] = { "zero", "one" };

int leaf(int *p, enum mode m)
{
	return *p + (int)m;
}

int middle(int a, struct point pt, enum mode m, const struct point *pp)
{
	int loc = a + pt.x;
	u32 arr[4] = { 1, 2, 3, 4 };
	static int calls = 5;
	struct point copy = *pp;

	calls++;
	loc += leaf(&loc, m) + (int)arr[2] + calls + counter + copy.y;
	return loc;
}

int main(void)
{
	struct point p = { 10, 20, "p" };

	return middle(1, p, FAST, &gp) == 0 && names[0] != 0;
}
EOF2

cmds() {
	gdb -batch -nx -ex 'break leaf' -ex run -ex bt -ex 'info args' -ex up -ex 'info args' -ex 'info locals' \
		-ex 'p pt' -ex 'p *pp' -ex 'p pp->name' -ex 'p arr' -ex 'p calls' -ex 'p gp' -ex 'p counter' \
		-ex 'p names' -ex 'ptype struct point' -ex 'ptype m' -ex 'whatis arr' -ex 'whatis loc' -ex 'p m' \
		-ex 'p/x copy' -ex 'finish' "$1" 2>&1 |
		grep -v -e '^\[' -e 'Download' -e 'debuginfod' -e '^Using' -e '^$' -e 'warning: Error disabling' |
		sed -e 's/0x[0-9a-f]*/ADDR/g' -e 's/Run till exit.*//' -e 's/^Value returned.*//'
}

fail=0
for m in 64 32; do
	gcc -m$m -std=gnu17 -g -O0 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c" 2>/dev/null || continue
	cmds "$CC_TMP/g" >"$CC_TMP/want"
	if ! grep -q 'leaf (p=' "$CC_TMP/want"; then
		echo "run_debug_gdb: -m$m: skip (gdb cannot run gcc's program here)"
		continue
	fi
	for path in mir legacy; do
		run=env
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -g -O0 $path"
		if ! $run "$CC_BIN" -m$m -std=gnu17 -g -O0 -c -o "$CC_TMP/c.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_debug_gdb: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o"
		cmds "$CC_TMP/c" >"$CC_TMP/got"
		if ! cmp -s "$CC_TMP/got" "$CC_TMP/want"; then
			echo "run_debug_gdb: $what: gdb shows cc's program otherwise than gcc's:"
			diff "$CC_TMP/want" "$CC_TMP/got" | head -20
			fail=1
		fi
	done
	# -O2: what gdb prints of a variable is its value or <optimized out>, never another
	"$CC_BIN" -m$m -std=gnu17 -g -O2 -c -o "$CC_TMP/o.o" "$CC_TMP/p.c"
	gcc -m$m -no-pie -o "$CC_TMP/o" "$CC_TMP/o.o"
	gdb -batch -nx -ex 'break leaf' -ex run -ex 'p *p' -ex 'p m' -ex up -ex 'p a' -ex 'p loc' -ex 'p pt.x' \
		-ex 'p gp.y' -ex 'p calls' "$CC_TMP/o" 2>&1 | grep '^\$' >"$CC_TMP/o.out" || true
	if grep -v -e '<optimized out>' -e '= 11$' -e '= FAST$' -e '= 1$' -e '= 10$' -e '= 4$' -e '= 6$' \
	    "$CC_TMP/o.out" | grep -q .; then
		echo "run_debug_gdb: -m$m -O2: gdb prints a value the program does not hold:"
		cat "$CC_TMP/o.out"
		fail=1
	fi
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_debug_gdb: ok"
