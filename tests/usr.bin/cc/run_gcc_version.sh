#!/bin/sh
# cc answers to one gcc version everywhere -- __GNUC__, __GNUC_MINOR__ and
# __GNUC_PATCHLEVEL__, --version and -dumpversion -- and it is 13.3 or
# later: Linux holds 13.0 to 13.2 to have broken asm goto outputs
# (GCC_ASM_GOTO_OUTPUT_BROKEN) and so built cc's kernels without them,
# though cc's work.  The two asm goto programs Linux's Kconfig compiles to
# decide CC_HAS_ASM_GOTO_OUTPUT and CC_HAS_ASM_GOTO_TIED_OUTPUT compile.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

macros=$("$CC_BIN" -dM -E -x c /dev/null)
major=$(printf '%s\n' "$macros" | awk '$2 == "__GNUC__" {print $3}')
minor=$(printf '%s\n' "$macros" | awk '$2 == "__GNUC_MINOR__" {print $3}')
patch=$(printf '%s\n' "$macros" | awk '$2 == "__GNUC_PATCHLEVEL__" {print $3}')
v="$major.$minor.$patch"
if [ "$("$CC_BIN" -dumpversion)" != "$v" ]; then
	echo "run_gcc_version: -dumpversion says $("$CC_BIN" -dumpversion), the macros $v"
	exit 1
fi
if ! "$CC_BIN" --version | head -1 | grep -qF " $v"; then
	echo "run_gcc_version: --version says [$("$CC_BIN" --version | head -1)], the macros $v"
	exit 1
fi
# Linux's GCC_VERSION, as scripts/cc-version.sh makes it
num=$((major * 10000 + minor * 100 + patch))
if [ "$num" -lt 130300 ]; then
	echo "run_gcc_version: cc says gcc $v, which Linux takes for one with broken asm goto outputs"
	exit 1
fi
printf 'int foo(int x) { asm goto ("": "=r"(x) ::: bar); return x; bar: return 0; }\n' >"$CC_TMP/a.c"
printf 'int foo(int *x) { asm goto (".long (%%l[bar]) - .": "+m"(*x) ::: bar); return *x; bar: return 0; }\n' \
	>"$CC_TMP/t.c"
for f in a t; do
	if ! "$CC_BIN" -x c -c "$CC_TMP/$f.c" -o "$CC_TMP/$f.o"; then
		echo "run_gcc_version: Kconfig's asm goto output probe $f.c does not compile"
		exit 1
	fi
done
echo "run_gcc_version: ok"
