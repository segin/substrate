#!/bin/sh
# __attribute__((patchable_function_entry(n, m))) gives its function its
# own -fpatchable-function-entry=n,m, as gcc does -- (0, 0) none, which
# is Linux's notrace where its ftrace patches those nops.  cc did not
# know the attribute: such a function had the command line's nops, or
# none.  Each function has as many nops before and after its label as
# gcc's, whether the attribute is on its definition or a declaration
# before it, and the object records as many entries as gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objcopy >/dev/null 2>&1 ||
	! command -v readelf >/dev/null 2>&1 || ! command -v od >/dev/null 2>&1; then
	echo "run_patchable_function_entry_attr: skip (no gcc, objcopy, readelf or od)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#if !__has_attribute(patchable_function_entry)
#error no patchable_function_entry
#endif
#define notrace __attribute__((patchable_function_entry(0, 0)))

int plain(int x) { return x + 1; }
notrace int quiet(int x) { return x * 2; }
__attribute__((patchable_function_entry(3, 1))) int own(int x) { return x - 3; }
__attribute__((patchable_function_entry(2))) int after(int x);
int after(int x) { return x ^ 4; }
int last(int x) { return x | 8; }
EOF2
# "before after" nop counts of function $2 in object $1
nops() {
	objcopy -O binary -j .text "$1" "$CC_TMP/text.bin"
	at=$((0x$(readelf -sW "$1" | awk -v f="$2" '$8 == f { print $2 }')))
	b=0
	while [ $((at - b - 1)) -ge 0 ] &&
		[ "$(od -An -tx1 -j $((at - b - 1)) -N1 "$CC_TMP/text.bin" | tr -d ' \n')" = 90 ]; do
		b=$((b + 1))
	done
	a=0
	while [ "$(od -An -tx1 -j $((at + a)) -N1 "$CC_TMP/text.bin" | tr -d ' \n')" = 90 ]; do
		a=$((a + 1))
	done
	echo "$b $a"
}
# the entries the object records, in however many sections of the name
entries() {
	readelf -rW "$1" | awk '/^Relocation section/ { on = $3 ~ /__patchable_function_entries/; next }
		on && /R_(X86_64|386)_/ { c++ } END { print c + 0 }'
}
for m in 64 32; do
	for f in "" -fpatchable-function-entry=5,2; do
		# shellcheck disable=SC2086
		gcc -m$m -O0 -fno-pic $f -c -o "$CC_TMP/g.o" "$CC_TMP/b.c" 2>/dev/null || continue
		for o in O0 O2; do
			for path in mir legacy; do
				run="env"
				if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
				what="-m$m -$o $path [$f]"
				# shellcheck disable=SC2086
				if ! $run "$CC_BIN" -m$m -$o $f -c -o "$CC_TMP/b.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
					echo "run_patchable_function_entry_attr: $what: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
				for fn in plain quiet own after last; do
					want=$(nops "$CC_TMP/g.o" $fn)
					got=$(nops "$CC_TMP/b.o" $fn)
					if [ "$got" != "$want" ]; then
						echo "run_patchable_function_entry_attr: $what: $fn has nops [$got] before and after, gcc's [$want]"
						exit 1
					fi
				done
				if [ "$(entries "$CC_TMP/b.o")" != "$(entries "$CC_TMP/g.o")" ]; then
					echo "run_patchable_function_entry_attr: $what: $(entries "$CC_TMP/b.o") entries, gcc's $(entries "$CC_TMP/g.o")"
					exit 1
				fi
			done
		done
	done
done
printf '__attribute__((patchable_function_entry(2, 3))) int bad(int x) { return x; }\n' >"$CC_TMP/n.c"
if "$CC_BIN" -c -o "$CC_TMP/n.o" "$CC_TMP/n.c" 2>"$CC_TMP/c.err"; then
	echo "run_patchable_function_entry_attr: patchable_function_entry(2, 3) was taken"
	exit 1
fi
echo "run_patchable_function_entry_attr: ok"
