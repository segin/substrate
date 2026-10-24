#!/bin/sh
# A _Bool local cleared by `&= 0` is known false, and what only it lets
# run is gone -- here a call of a function no object defines -- as with
# gcc from -O1: the local was kept in memory, its stores of a wider value
# being narrower than it, and Linux's copy_fpstate_to_sigframe(), whose
# `ia32_fxstate &= (IS_ENABLED(CONFIG_X86_32) || ...)` makes its call of
# convert_from_fxsr() dead on x86-64 without IA32 emulation, left vmlinux
# with that function undefined.  codegen/narrow_store_promote.c checks
# the values of such locals.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/dead.c" <<'EOF'
extern void missing(void);

static inline __attribute__((always_inline)) int need(void) {
    missing();
    return 1;
}

int f(void *a, void *b) {
    _Bool x = (a != b);

    x &= (0 || 0);
    if ((x || !1) && !need())
        return 1;
    return 0;
}
EOF
for m in 32 64; do
	for o in O1 O2 Os; do
		"$CC_BIN" -m$m -$o -c -o "$CC_TMP/dead.o" "$CC_TMP/dead.c"
		if nm -u "$CC_TMP/dead.o" | grep -q missing; then
			echo "run_dead_bool_call: -m$m -$o calls the function the cleared bool rules out"
			exit 1
		fi
	done
done
echo "run_dead_bool_call: ok"
