#!/bin/sh
# x86-64 position-independent code reaches what binds within the object
# -- a static, a static local, a global defined here under -fPIE, one of
# hidden visibility, defined or declared so -- relative to %rip, not
# through the GOT, as gcc does; only what another module may supply has
# a GOT entry.  Every symbol went through the GOT, and Linux's x86-64
# decompressor, built -fPIE with every declaration hidden, asserts it has
# none: "Unexpected GOT entries detected!".
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/pie.c" <<'EOF2'
#pragma GCC visibility push(hidden)
extern int hid;
extern int tab[4];
int defd;
static int st;

int get(void) {
    static int loc;

    return hid + tab[2] + defd + st + loc++;
}

int *addr(void) {
    return &hid;
}
#pragma GCC visibility pop

extern int vis;

int getv(int n, ...) {
    return vis + n;
}

int getw(void) {
    return vis;
}
EOF2
for f in -fPIE -fPIC; do
	for o in O0 O2; do
		"$CC_BIN" -m64 $f -$o -c -o "$CC_TMP/pie.o" "$CC_TMP/pie.c"
		got=$(objdump -dr "$CC_TMP/pie.o" | grep -oE 'GOT[A-Z]*[[:space:]]+[a-z_.0-9]+' | awk '{print $2}' |
			sort -u | tr '\n' ' ')
		if [ "$got" != "vis " ]; then
			echo "run_x64_local_no_got: $f -$o reaches [$got] through the GOT, not only vis"
			exit 1
		fi
	done
done
echo "run_x64_local_no_got: ok"
