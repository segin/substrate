#!/bin/sh
# -ffunction-sections and -fdata-sections put each function and each
# object in a section of its own -- .text.<name> (.text.hot.<name>,
# .text.unlikely.<name> for hot and cold ones), .data.<name>,
# .bss.<name> -- as gcc names them, so that a link with --gc-sections
# drops what nothing uses.  Substrate's x86-64 kernel builds with both.
# cc took them and ignored them.  Each symbol is in gcc's section, but
# for one named by a section attribute, which keeps it; a link with
# --gc-sections drops the unused function and object; the program runs
# as gcc's; and with neither option all is in .text, .data and .bss.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v readelf >/dev/null 2>&1; then
	echo "run_function_sections: skip (no gcc or readelf)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

int counter = 7;
int unused_data = 9;
int zeroed;
static int hidden_count = 3;
__attribute__((section(".mine"))) int placed = 5;

__attribute__((noinline)) int used(int x) { return x + counter + hidden_count++; }
__attribute__((noinline)) int unused(int x) { return x * unused_data; }
__attribute__((noinline, cold)) int rare(int x) { return x - 1; }
__attribute__((noinline, hot)) int often(int x) { return x + 1; }
__attribute__((noinline, section(".mine.text"))) int named(int x) { return x ^ placed; }

int main(void)
{
	zeroed = used(1);
	printf("%d %d %d %d\n", zeroed, rare(5), often(6), named(1));
	return 0;
}
EOF2
# symbol -> its section's name, for the functions and objects of the source
placement() {
	readelf -SW "$1" | sed -n 's/^ *\[ *\([0-9]*\)\] \([^ ]*\).*/\1 \2/p' >"$CC_TMP/secs"
	# (not main, which gcc's -O2 puts in .text.startup, and cc does not)
	readelf -sW "$1" | awk -v want=" counter unused_data zeroed placed used unused rare often named " \
		'NR == FNR { name[$1] = $2; next }
		index(want, " " $8 " ") { print $8, name[$7] }' "$CC_TMP/secs" - | sort
}
for m in 64 32; do
	gcc -m$m -c -o "$CC_TMP/probe.o" "$CC_TMP/b.c" 2>/dev/null || continue
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/b.c"
	want_out=$("$CC_TMP/g")
	# (-O2: gcc's hot and cold sections are -freorder-functions', which -O1 lacks; cc's are the attributes')
	gcc -m$m -O2 -fno-pic -ffunction-sections -fdata-sections -c -o "$CC_TMP/g.o" "$CC_TMP/b.c"
	want=$(placement "$CC_TMP/g.o")
	for o in O0 O2; do
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			what="-m$m -$o $path"
			if ! $run "$CC_BIN" -m$m -$o -ffunction-sections -fdata-sections -c -o "$CC_TMP/c.o" "$CC_TMP/b.c" \
				2>"$CC_TMP/c.err"; then
				echo "run_function_sections: $what: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			got=$(placement "$CC_TMP/c.o")
			if [ "$got" != "$want" ]; then
				echo "run_function_sections: $what places [$(echo "$got" | tr '\n' ';')], gcc [$(echo "$want" | tr '\n' ';')]"
				exit 1
			fi
			gcc -m$m -no-pie -Wl,--gc-sections -o "$CC_TMP/c" "$CC_TMP/c.o"
			if [ "$("$CC_TMP/c")" != "$want_out" ]; then
				echo "run_function_sections: $what prints [$("$CC_TMP/c")], gcc's [$want_out]"
				exit 1
			fi
			if nm "$CC_TMP/c" | grep -qw 'unused\|unused_data'; then
				echo "run_function_sections: $what: --gc-sections kept what nothing uses"
				exit 1
			fi
			$run "$CC_BIN" -m$m -$o -c -o "$CC_TMP/n.o" "$CC_TMP/b.c"
			if readelf -SW "$CC_TMP/n.o" | grep -q '\.text\.used\|\.data\.counter'; then
				echo "run_function_sections: $what: sections of their own with neither option"
				exit 1
			fi
		done
	done
done
echo "run_function_sections: ok"
