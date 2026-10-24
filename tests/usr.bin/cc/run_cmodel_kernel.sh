#!/bin/sh
# -mcmodel=kernel: a global declared extern is reached relative to %rip,
# not through the GOT -- read, written and its address taken, in a
# function the register allocator takes and in a variadic one the other
# emitter makes -- as with gcc.  Linux's x86-64 vmlinux.lds asserts that
# there is no GOT, and the link failed: "Unexpected GOT entries detected!".
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/k.c" <<'EOF'
extern int x;
extern int tab[4];

int get(void) {
    return x + tab[2];
}

int *addr(void) {
    return &x;
}

void put(int v) {
    x = v;
}

int sum(int n, ...) {
    return x + n;
}
EOF
for o in O0 O2; do
	"$CC_BIN" -m64 -mcmodel=kernel -$o -c -o "$CC_TMP/k.o" "$CC_TMP/k.c"
	if objdump -dr "$CC_TMP/k.o" | grep -q GOT; then
		echo "run_cmodel_kernel: -$o reaches a global through the GOT:"
		objdump -dr "$CC_TMP/k.o" | grep GOT
		exit 1
	fi
done
echo "run_cmodel_kernel: ok"
