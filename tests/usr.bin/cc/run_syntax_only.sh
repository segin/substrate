#!/bin/sh
# -fsyntax-only checks C and writes nothing, as gcc's: cc took it for a
# flag it could ignore, compiled and tried to link.  A valid file and
# one with an error in what it means (an undeclared name) and one in
# its syntax give gcc's exit status; no .s, .o or a.out appears, with
# -c, -S and -o or without; an assembly input is left alone.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_syntax_only: skip (no gcc)"
	exit 0
fi

work="$CC_TMP/syn"
rm -rf "$work"
mkdir -p "$work"
cd "$work"
printf 'int main(void) { return 0; }\n' >ok.c
printf 'int f(void) { return undeclared_name; }\n' >sema.c
printf 'int f(void) { return 1 +; }\n' >syntax.c
printf '\tbogus_instruction %%eax\n' >a.s

for f in ok sema syntax; do
	set +e
	gcc -fsyntax-only "$f.c" 2>/dev/null
	want=$?
	"$CC_BIN" -fsyntax-only "$f.c" 2>/dev/null
	got=$?
	set -e
	if [ $want -ne 0 ]; then want=1; fi
	if [ $got -ne 0 ]; then got=1; fi
	if [ "$got" != "$want" ]; then
		echo "run_syntax_only: $f.c exits $got, gcc $want"
		exit 1
	fi
done
for flags in "" "-c" "-S" "-c -o x.o" "-S -o x.s" "-o prog"; do
	# shellcheck disable=SC2086
	"$CC_BIN" -fsyntax-only $flags ok.c
	extra=$(ls | grep -v -x -e ok.c -e sema.c -e syntax.c -e a.s || true)
	if [ -n "$extra" ]; then
		echo "run_syntax_only: [$flags] wrote [$extra]"
		exit 1
	fi
done
if ! "$CC_BIN" -fsyntax-only a.s ok.c; then
	echo "run_syntax_only: an assembly input is not left alone"
	exit 1
fi
echo "run_syntax_only: ok"
