#!/bin/sh
# -fvisibility=hidden (and protected, internal, default) gives each
# definition that names no visibility of its own that one, as gcc does;
# a declaration of something defined elsewhere keeps the default, and
# an explicit attribute or #pragma GCC visibility wins.  Linux's kexec
# purgatory builds -fpic -fvisibility=hidden.  cc took the option and
# ignored it.  Each symbol of the object has the visibility gcc's has,
# -m64 and -m32, through the MIR path and the legacy emitters.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v readelf >/dev/null 2>&1; then
	echo "run_fvisibility: skip (no gcc or readelf)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
extern int elsewhere;
extern int called(int);
int defined_data = 3;
int tentative;
const char text[] = "x";
static int file_local;
int proto(int);
int proto(int x) { return x + file_local; }
__attribute__((visibility("default"))) int exported(int x) { return x * 2; }
__attribute__((visibility("protected"))) int prot = 4;
#pragma GCC visibility push(default)
int pushed(void) { return 5; }
#pragma GCC visibility pop
int user(void) { return elsewhere + called(defined_data) + tentative + text[0] + prot; }
EOF2
# each symbol's binding, visibility and whether it is defined, by name
vis() {
	# (not the locals, which have no visibility to speak of, and which gcc's optimizer may drop; nor the
	# GOT, which cc's x86-64 code reaches an extern variable through where gcc's does not; nor i386's pc
	# thunks, gcc's global in a group of their own, cc's local)
	readelf -sW "$1" | awk '$8 != "" && $5 != "LOCAL" && $4 != "SECTION" && $4 != "FILE" &&
		$8 != "_GLOBAL_OFFSET_TABLE_" && $8 !~ /^__x86\.get_pc_thunk\./ &&
		$8 !~ /^\./ && $8 !~ /^__cc_/ { print $8, $5, $6, ($7 == "UND" ? "und" : "def") }' | sort
}
for m in 64 32; do
	gcc -m$m -c -o "$CC_TMP/probe.o" "$CC_TMP/b.c" 2>/dev/null || continue
	for v in hidden protected internal default; do
		for pic in -fpic -fno-pic; do
			gcc -m$m -O1 $pic -fvisibility=$v -c -o "$CC_TMP/g.o" "$CC_TMP/b.c"
			want=$(vis "$CC_TMP/g.o")
			for o in O0 O2; do
				for path in mir legacy; do
					run="env"
					if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
					what="-m$m -$o $path $pic -fvisibility=$v"
					if ! $run "$CC_BIN" -m$m -$o $pic -fvisibility=$v -c -o "$CC_TMP/c.o" "$CC_TMP/b.c" \
						2>"$CC_TMP/c.err"; then
						echo "run_fvisibility: $what: $(head -1 "$CC_TMP/c.err")"
						exit 1
					fi
					got=$(vis "$CC_TMP/c.o")
					if [ "$got" != "$want" ]; then
						echo "run_fvisibility: $what:"
						echo "$got" | diff - "$(printf '%s\n' "$want" >"$CC_TMP/want"; echo "$CC_TMP/want")" | head -10
						exit 1
					fi
				done
			done
		done
	done
done
if "$CC_BIN" -fvisibility=bogus -c -o "$CC_TMP/n.o" "$CC_TMP/b.c" 2>/dev/null; then
	echo "run_fvisibility: -fvisibility=bogus was taken"
	exit 1
fi
echo "run_fvisibility: ok"
