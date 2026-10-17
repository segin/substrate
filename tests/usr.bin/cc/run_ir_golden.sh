#!/bin/sh
# run_ir_golden.sh: the IR golden tests.
#
# Each ir/NAME.c is compiled with -fdump-ir at -m64 and -m32, and the dump
# is compared with ir/NAME.m64.ir and ir/NAME.m32.ir by ir-diff, which
# verifies both and compares them with their values renumbered.  Then
# every codegen/*.c is dumped after each middle-end pass (-fdump-ir=all)
# at both widths, and every dump must pass ir-verifier.
#
# UPDATE=1 rewrites the golden files from the current compiler instead
# of comparing; review the change before committing it.
# CC_BIN names another cc to test.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN=${CC_BIN:-"$ROOT/usr.bin/cc/cc"}
IR_BIN=${IR_BIN:-"$ROOT/usr.bin/cc"}
HERE="$ROOT/tests/usr.bin/cc"
UPDATE=${UPDATE:-0}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_ir_golden.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

fail=0
runs=0
# run from the test directory so the dump's module name is the relative path
cd "$HERE" || exit 1

for src in ir/*.c; do
	name=$(basename "$src" .c)
	for m in 64 32; do
		runs=$((runs + 1))
		golden="ir/$name.m$m.ir"
		if ! "$CC_BIN" -m$m -S -fdump-ir "$src" -o "$tmp/$name.s" 2>"$tmp/log"; then
			echo "FAIL $name -m$m: cc failed"
			sed 's/^/  /' "$tmp/log"
			fail=1
			continue
		fi
		if [ "$UPDATE" = 1 ]; then
			cp "$tmp/$name.ir" "$golden"
			echo "updated $golden"
			continue
		fi
		if [ ! -f "$golden" ]; then
			echo "FAIL $name -m$m: no $golden (UPDATE=1 writes it)"
			fail=1
			continue
		fi
		if ! "$IR_BIN/ir-diff" "$golden" "$tmp/$name.ir" >"$tmp/diff" 2>&1; then
			echo "FAIL $name -m$m: the IR differs from $golden"
			sed 's/^/  /' "$tmp/diff" | head -40
			fail=1
		fi
	done
done

for src in codegen/*.c; do
	name=$(basename "$src" .c)
	# a test may name its language in a "cc-std: <std>" comment
	std=$(sed -n 's/.*cc-std: *\([a-z0-9]*\).*/\1/p' "$src" | head -n 1)
	for m in 64 32; do
		runs=$((runs + 1))
		if ! "$CC_BIN" -m$m ${std:+-std=$std} -S -fdump-ir=all "$src" -o "$tmp/$name.s" 2>"$tmp/log"; then
			echo "FAIL $name -m$m: cc failed"
			sed 's/^/  /' "$tmp/log"
			fail=1
			continue
		fi
		for pass in lower legalize opt; do
			if ! "$IR_BIN/ir-verifier" "$tmp/$name.$pass.ir" >"$tmp/log" 2>&1; then
				echo "FAIL $name -m$m: the IR after $pass does not verify"
				sed 's/^/  /' "$tmp/log"
				fail=1
			fi
		done
		rm -f "$tmp/$name".*
	done
done

if [ "$fail" -ne 0 ]; then
	echo "run_ir_golden: FAILED"
	exit 1
fi
echo "run_ir_golden: $runs runs passed"
