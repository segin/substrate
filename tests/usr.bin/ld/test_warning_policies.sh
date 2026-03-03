#!/bin/sh
set -eu

# Reqs: LD-W-003, LD-E-001

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-warn-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/common.c" <<'SRC'
int common_sym;
int common_user(void) { return common_sym; }
SRC
gcc -m64 -fcommon -c -o "$TMP/common.o" "$TMP/common.c"

"$LDX" -m64 -r --warn-common -o "$TMP/out_common_warn.o" "$TMP/common.o" >"$TMP/common_warn.out" 2>&1
if ! grep -q "common symbol" "$TMP/common_warn.out"; then
	echo "FAIL: --warn-common did not emit warning" >&2
	cat "$TMP/common_warn.out" >&2
	exit 1
fi

if "$LDX" -m64 -r --warn-common --fatal-warnings -o "$TMP/out_common_fatal.o" "$TMP/common.o" >"$TMP/common_fatal.out" 2>&1; then
	echo "FAIL: --fatal-warnings did not fail on --warn-common warning" >&2
	exit 1
fi
if ! grep -q "warnings treated as errors" "$TMP/common_fatal.out"; then
	echo "FAIL: missing fatal-warnings diagnostic" >&2
	cat "$TMP/common_fatal.out" >&2
	exit 1
fi

if "$LDX" -m64 -r --fatal-warnings --unknown-warning-source -o "$TMP/out_unknown_fatal.o" "$TMP/common.o" >"$TMP/unknown_fatal.out" 2>&1; then
	echo "FAIL: --fatal-warnings did not fail on generic warning path" >&2
	exit 1
fi
if ! grep -q "unsupported option ignored" "$TMP/unknown_fatal.out"; then
	echo "FAIL: missing unsupported-option warning under --fatal-warnings" >&2
	cat "$TMP/unknown_fatal.out" >&2
	exit 1
fi

cat > "$TMP/unresolved.c" <<'SRC'
extern int missing_symbol;
int unresolved_ref(void) { return missing_symbol; }
SRC
gcc -m64 -c -o "$TMP/unresolved.o" "$TMP/unresolved.c"

if "$LDX" -m64 -static -e unresolved_ref -o "$TMP/out_report_default" "$TMP/unresolved.o" >"$TMP/unresolved_default.out" 2>&1; then
	echo "FAIL: default unresolved policy unexpectedly linked executable" >&2
	exit 1
fi

"$LDX" -m64 -static -e unresolved_ref --unresolved-symbols=ignore-all -o "$TMP/out_ignore_all" "$TMP/unresolved.o"

if "$LDX" -m64 -static -e unresolved_ref --unresolved-symbols=report-all -o "$TMP/out_report_all" "$TMP/unresolved.o" >"$TMP/unresolved_report.out" 2>&1; then
	echo "FAIL: report-all unresolved policy unexpectedly linked executable" >&2
	exit 1
fi

if "$LDX" -m64 -static -e unresolved_ref --unresolved-symbols=bogus -o "$TMP/out_bad_policy" "$TMP/unresolved.o" >"$TMP/unresolved_bad.out" 2>&1; then
	echo "FAIL: invalid --unresolved-symbols policy unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "unsupported --unresolved-symbols policy" "$TMP/unresolved_bad.out"; then
	echo "FAIL: missing invalid unresolved-policy diagnostic" >&2
	cat "$TMP/unresolved_bad.out" >&2
	exit 1
fi

echo "ok: ld warning and unresolved policy options"
