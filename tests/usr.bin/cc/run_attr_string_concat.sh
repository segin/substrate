#!/bin/sh
# An attribute's string argument may be adjacent literals, one string as
# anywhere else in C.  cc kept the first: Linux's DEFINE_SCHED_CLASS
# names its section "__" #name "_sched_class", every class went into one
# section "__" in no order, and sched_init()'s check that they lie
# highest first failed with BUG().
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/sc.c" <<'EOF'
struct sc { int a; };
const struct sc stop_sc __attribute__((__section__("__" "stop" "_sched_class"))) = { 1 };
void hot(void) __attribute__((section(".text." "hot")));
void hot(void) {}
EOF
status=0
for m in 32 64; do
	"$CC_BIN" -m$m -S -o "$CC_TMP/sc.s" "$CC_TMP/sc.c"
	for want in __stop_sched_class .text.hot; do
		if ! grep -q "\.section $want," "$CC_TMP/sc.s"; then
			echo "FAIL -m$m: no section $want"
			status=1
		fi
	done
done
[ "$status" -eq 0 ] && echo "run_attr_string_concat: ok"
exit "$status"
