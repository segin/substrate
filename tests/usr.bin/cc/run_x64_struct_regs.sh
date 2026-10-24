#!/bin/sh
# An x86-64 function that takes or passes a small structure by value in
# general registers goes through the register allocator, as one taking
# scalars does.  It did not: such a function fell back to the emitter
# that gives every value a stack slot of its own, and Linux, which passes
# struct timespec64 so throughout, had frames past the 16 KiB of a kernel
# stack (notify_change(), 26 KiB) -- the stack overran into whatever lay
# below it and the kernel faulted at random.  codegen/struct_by_value_regs.c
# checks the values such calls pass.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/ts.c" <<'EOF2'
struct timespec64 {
    long tv_sec;
    long tv_nsec;
};

struct small {
    int a;
    int b;
    int c;
};

struct timespec64 add(struct timespec64 a, struct timespec64 b);

long sec_of(struct timespec64 t) {
    return t.tv_sec;
}

long pass(struct timespec64 *p, struct small s) {
    struct timespec64 t = add(*p, *p);

    return t.tv_nsec + s.c + sec_of(t);
}
EOF2
for o in O1 O2; do
	CC_MIR_STATS=1 "$CC_BIN" -m64 -$o -c -o "$CC_TMP/ts.o" "$CC_TMP/ts.c" 2> "$CC_TMP/stats"
	for fn in sec_of pass; do
		if ! grep -q "^mir: $fn: taken" "$CC_TMP/stats"; then
			echo "run_x64_struct_regs: -$o: $fn is not taken by the register allocator:"
			cat "$CC_TMP/stats"
			exit 1
		fi
	done
done
echo "run_x64_struct_regs: ok"
