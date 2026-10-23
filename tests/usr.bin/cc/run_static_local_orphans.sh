#!/bin/sh
# A static inline nothing calls is not compiled, and leaves nothing
# behind: its block-scope statics went into the object all the same, and
# once such a table of pointers was laid out as data it had relocations --
# Linux's vDSO, which must have none, got of_overlay_action_name()'s.
# One that is called keeps its table.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="${CC_BIN:-$ROOT/usr.bin/cc/cc}"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

status=0
cat > "$CC_TMP/unused.c" <<'EOF'
static inline const char *unused_name(int i) {
	static const char *const names[] = {"one", "two"};

	return names[i];
}
static inline const char *used_name(int i) {
	static const char *const kept[] = {"three", "four"};

	return kept[i];
}
const char *pick(int i) { return used_name(i); }
EOF
for m in 32 64; do
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -S -o "$CC_TMP/unused.s" "$CC_TMP/unused.c"
		if grep -q "unused_name_names" "$CC_TMP/unused.s"; then
			echo "FAIL -m$m -$o: the table of a static inline nothing calls was emitted"
			status=1
		fi
		if ! grep -q "used_name_kept" "$CC_TMP/unused.s"; then
			echo "FAIL -m$m -$o: the table of a static inline that is called was dropped"
			status=1
		fi
	done
done
[ "$status" -eq 0 ] && echo "run_static_local_orphans: ok"
exit "$status"
