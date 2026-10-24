#!/bin/sh
# i386 values live in registers, not frame slots: a loop down a list that
# sums two fields keeps the pointer, the limit and both sums in registers
# all the way through, so the function touches the frame only to read its
# two parameters -- as with gcc.  The emitter that kept every value in its
# frame slot read and wrote the sums and the pointer there on every trip
# round the loop.  codegen/regalloc_pressure.c checks the values of code
# that needs more registers than there are.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/walk.c" <<'EOF'
struct node {
    struct node *next;
    int key;
    int val;
};

int walk(struct node *n, int lim) {
    int s = 0, c = 0;

    for (; n; n = n->next) {
        if (n->key > lim)
            s += n->val * 3;
        else
            s -= n->val;
        c++;
    }
    return s + c;
}
EOF
for o in O1 O2 Os; do
	"$CC_BIN" -m32 -$o -S -o "$CC_TMP/walk.s" "$CC_TMP/walk.c"
	n=$(sed -n '/^walk:/,/^\.size/p' "$CC_TMP/walk.s" | grep -c '(%ebp)' || true)
	if [ "$n" -gt 2 ]; then
		echo "run_i386_regalloc: walk -$o reads or writes the frame $n times, not 2:"
		sed -n '/^walk:/,/^\.size/p' "$CC_TMP/walk.s"
		exit 1
	fi
done
echo "run_i386_regalloc: ok"
