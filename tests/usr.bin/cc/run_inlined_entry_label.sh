#!/bin/sh
# A function whose first instruction, once a call is inlined, is a label
# of the inlined body: the code from the function's entry runs.  A switch
# on a constant left an inlined body beginning with a label; the folder
# took a label nothing branched to for code that does not run, the
# fall-through of an asm goto after it never joined, and the inlined
# function's result was what its label alone returned.  Linux's static
# branches are such asm gotos: kvm_is_using_evmcs() read true, and KVM
# refused to start ("kvm: enabling virtualization on CPU0 failed").
# gcc gives 1; so must cc, at -O1, -O2 and -Os, on both targets.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/e.c" <<'EOF2'
int unused;

static inline __attribute__((always_inline)) _Bool falls(void)
{
	switch (sizeof(int)) {
	case 4: break;
	default: unused = 1; break;
	}
	asm goto("" : : : : yes);
	return 0;
yes:
	return 1;
}

static inline __attribute__((always_inline)) _Bool jumps(void)
{
	switch (sizeof(int)) {
	case 4: break;
	default: unused = 1; break;
	}
	asm goto("jmp %l[yes]" : : : : yes);
	return 0;
yes:
	return 1;
}

__attribute__((noinline)) int fell(void)
{
	if (falls())
		return 10;
	return 0;
}

__attribute__((noinline)) int jumped(void)
{
	if (jumps())
		return 1;
	return 0;
}

int main(void)
{
	return fell() + jumped() == 1 ? 0 : 1;
}
EOF2
if command -v gcc >/dev/null 2>&1 && gcc -O2 -o "$CC_TMP/g" "$CC_TMP/e.c" 2>/dev/null && ! "$CC_TMP/g"; then
	echo "run_inlined_entry_label: skip (gcc gives otherwise)"
	exit 0
fi
for m in 64 32; do
	for o in O1 O2 Os; do
		"$CC_BIN" -m$m -$o -o "$CC_TMP/e" "$CC_TMP/e.c"
		if ! "$CC_TMP/e"; then
			echo "run_inlined_entry_label: -m$m -$o: the asm goto's fall-through gives its label's result"
			exit 1
		fi
	done
done
echo "run_inlined_entry_label: ok"
