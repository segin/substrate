#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TDIR="$ROOT/tests/usr.bin/ld"
TMP=${TMPDIR:-/tmp}/ldx86-testdash-$$
REQ_FILE="$TMP/req_results.txt"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

total=0
passed=0
failed=0

echo "== ld test dashboard =="
echo "root: $ROOT"
echo

echo "Building ld (NATIVE_BUILD=1)..."
make -C "$ROOT/usr.bin/ld" NATIVE_BUILD=1 -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)" >/dev/null
echo "Build: PASS"
echo

while IFS= read -r line; do
	[ -z "$line" ] && continue
	test_name=${line%%|*}
	reqs=${line#*|}
	total=$((total + 1))
	printf "[RUN ] %s\n" "$test_name"
	if sh "$TDIR/$test_name" >"$TMP/$test_name.out" 2>&1; then
		passed=$((passed + 1))
		printf "[PASS] %s\n" "$test_name"
		result=1
	else
		failed=$((failed + 1))
		printf "[FAIL] %s\n" "$test_name"
		sed -n '1,120p' "$TMP/$test_name.out"
		result=0
	fi
	for req in $reqs; do
		printf "%s %s\n" "$req" "$result" >>"$REQ_FILE"
	done
done <<'EOF'
test_link_32_64.sh|LD-U-002 LD-U-003
test_mode_parser.sh|LD-U-010 LD-E-007
test_unsupported_option_policy.sh|LD-U-010 LD-W-003
test_entry_option.sh|LD-U-001 LD-U-009
test_library_search_modes.sh|LD-U-004
EOF

echo
echo "== requirement summary =="
if [ -f "$REQ_FILE" ]; then
	awk '
	{
		req=$1;
		pass=$2;
		total[req] += 1;
		ok[req] += pass;
	}
	END {
		for (r in total) {
			printf "%s: %d/%d passing\n", r, ok[r], total[r];
		}
	}
	' "$REQ_FILE" | sort
else
	echo "no requirement data produced"
fi

echo
echo "== totals =="
echo "passed: $passed"
echo "failed: $failed"
echo "total : $total"

if [ "$failed" -ne 0 ]; then
	exit 1
fi
exit 0
