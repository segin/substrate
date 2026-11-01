#!/bin/sh
# -pg calls __fentry__ at each function's entry (-mfentry, x86-64's
# default) or mcount once the frame is made (i386's default,
# -mno-fentry), as gcc does; Linux's ftrace patches the former.  cc
# took -pg, -mfentry and -mrecord-mcount and ignored them: a kernel
# built with CONFIG_FUNCTION_TRACER had no call to patch.  A function
# declared no_instrument_function (Linux's notrace) has none.  The
# program, linked with a __fentry__ and an mcount that count their
# calls, counts what gcc's does; -mrecord-mcount puts one __mcount_loc
# entry per instrumented function, at its call; -mnop-mcount makes each
# call a 5-byte nop; a host program linked with -pg writes gmon.out,
# gcrt1.o's.  -pg with -fomit-frame-pointer, and i386's with -fpic,
# are refused, as gcc refuses them.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objdump >/dev/null 2>&1 ||
	! command -v readelf >/dev/null 2>&1; then
	echo "run_profile_entry: skip (no gcc, objdump or readelf)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

extern int prof_hits;

__attribute__((noinline)) long leaf(long x) { return x * 3 + 1; }
__attribute__((noinline, no_instrument_function)) long quiet(long x) { return x - 2; }
static inline __attribute__((__no_instrument_function__)) long quiet_inline(long x) { return x ^ 5; }
__attribute__((noinline)) long walk(int n)
{
	long t = 0;
	int i;

	for (i = 0; i < n; ++i) {
		t += leaf(i) + quiet(i) + quiet_inline(i);
	}
	return t;
}

int main(void)
{
	long t = walk(10);

	printf("%ld %d\n", t, prof_hits);
	return 0;
}
EOF2
for m in 64 32; do
	{
		printf '\t.data\n\t.globl prof_hits\nprof_hits:\t.long 0\n\t.text\n'
		for s in __fentry__ mcount; do
			if [ $m = 64 ]; then inc="incl prof_hits(%rip)"; else inc="incl prof_hits"; fi
			printf '\t.globl %s\n%s:\n\t%s\n\tret\n' "$s" "$s" "$inc"
		done
	} >"$CC_TMP/prof.s"
	gcc -m$m -c -o "$CC_TMP/prof.o" "$CC_TMP/prof.s" 2>/dev/null || continue
	if [ $m = 64 ]; then kinds="-mfentry -mno-fentry"; else kinds="-mfentry -mno-fentry"; fi
	for kind in "" $kinds; do
		if [ -z "$kind" ]; then
			if [ $m = 64 ]; then sym=__fentry__; else sym=mcount; fi
		elif [ "$kind" = -mfentry ]; then
			sym=__fentry__
		else
			sym=mcount
		fi
		# shellcheck disable=SC2086
		gcc -m$m -O1 -fno-pic -pg $kind -c -o "$CC_TMP/g.o" "$CC_TMP/b.c" 2>/dev/null
		gcc -m$m -no-pie -o "$CC_TMP/g" "$CC_TMP/g.o" "$CC_TMP/prof.o"
		want=$("$CC_TMP/g")
		for o in O0 O2; do
			for path in mir legacy; do
				run="env"
				if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
				what="-m$m -$o $path [$kind]"
				# shellcheck disable=SC2086
				if ! $run "$CC_BIN" -m$m -$o -pg $kind -mrecord-mcount -c -o "$CC_TMP/b.o" "$CC_TMP/b.c" \
					2>"$CC_TMP/c.err"; then
					echo "run_profile_entry: $what: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
				gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/b.o" "$CC_TMP/prof.o"
				got=$("$CC_TMP/c")
				if [ "$got" != "$want" ]; then
					echo "run_profile_entry: $what prints [$got], gcc's [$want]"
					exit 1
				fi
				# one __mcount_loc entry per instrumented function (leaf, walk, main), each at a call of $sym
				n=$(readelf -rW "$CC_TMP/b.o" | awk '/^Relocation section/ { on = $3 ~ /__mcount_loc/ } on && /\.text/ { c++ } END { print c + 0 }')
				if [ "$n" != 3 ]; then
					echo "run_profile_entry: $what: $n __mcount_loc entries, not 3"
					exit 1
				fi
				calls=$(objdump -dr "$CC_TMP/b.o" | grep -c "R_[0-9A-Z_]*[[:space:]]*$sym" || true)
				if [ "$calls" != 3 ]; then
					echo "run_profile_entry: $what: $calls calls of $sym, not 3"
					exit 1
				fi
				for fn in quiet; do
					if objdump -dr "$CC_TMP/b.o" | awk -v f="<$fn>:" '$2 == f { on = 1; next } /^[0-9a-f]+ </ { on = 0 } on' |
						grep -q "$sym"; then
						echo "run_profile_entry: $what: no_instrument_function $fn calls $sym"
						exit 1
					fi
				done
			done
		done
		# -mnop-mcount: the calls 5-byte nops, still recorded
		# shellcheck disable=SC2086
		"$CC_BIN" -m$m -O2 -pg $kind -mrecord-mcount -mnop-mcount -c -o "$CC_TMP/b.o" "$CC_TMP/b.c"
		if objdump -dr "$CC_TMP/b.o" | grep -q "$sym"; then
			echo "run_profile_entry: -m$m [$kind] -mnop-mcount: a call of $sym"
			exit 1
		fi
		nops=$(objdump -d "$CC_TMP/b.o" | grep -c '0f 1f 44 00 00' || true)
		if [ "$nops" != 3 ]; then
			echo "run_profile_entry: -m$m [$kind] -mnop-mcount: $nops 5-byte nops, not 3"
			exit 1
		fi
	done
	# no -pg: no call
	"$CC_BIN" -m$m -O2 -mfentry -mrecord-mcount -c -o "$CC_TMP/b.o" "$CC_TMP/b.c"
	if objdump -dr "$CC_TMP/b.o" | grep -q '__fentry__\|mcount'; then
		echo "run_profile_entry: -m$m without -pg: a call"
		exit 1
	fi
done
# gcrt1.o's startup writes gmon.out
if gcc -pg -o "$CC_TMP/gp" -x c - 2>/dev/null <<'EOF2'
int main(void) { return 0; }
EOF2
then
	cat >"$CC_TMP/m.c" <<'EOF2'
#include <stdio.h>
__attribute__((noinline)) int sq(int x) { return x * x; }
int main(void) { printf("%d\n", sq(7)); return 0; }
EOF2
	if ! "$CC_BIN" -pg -o "$CC_TMP/mp" "$CC_TMP/m.c" 2>"$CC_TMP/c.err"; then
		echo "run_profile_entry: cc -pg links nothing: $(head -1 "$CC_TMP/c.err")"
		exit 1
	fi
	rm -f "$CC_TMP/gmon.out"
	(cd "$CC_TMP" && ./mp >/dev/null)
	if [ ! -s "$CC_TMP/gmon.out" ]; then
		echo "run_profile_entry: a program linked with -pg wrote no gmon.out"
		exit 1
	fi
fi
for f in "-pg -fomit-frame-pointer" "-m32 -fPIC -pg" "-m32 -fPIC -pg -mfentry"; do
	# shellcheck disable=SC2086
	if "$CC_BIN" $f -c -o "$CC_TMP/n.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
		echo "run_profile_entry: [$f] was taken"
		exit 1
	fi
done
echo "run_profile_entry: ok"
