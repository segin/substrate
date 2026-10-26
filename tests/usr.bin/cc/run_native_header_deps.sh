#!/bin/sh
# A host build (NATIVE_BUILD=1) tracks header dependencies as a target
# build does: its compiles write the .d files Makefile.inc reads back, so
# an edit to a header rebuilds every object that includes it.  The host
# flags lacked -MMD -MP: cc_options.h's gcc version changed and
# frontend/preproc.o, built before, kept predefining the old __GNUC__ --
# and a new field in a shared structure left objects reading the old
# layout.  A copy of cc is built in a scratch tree, a header touched, and
# the objects that include it must be rebuilt, the others kept.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_native_header_deps: skip (no gcc)"
	exit 0
fi

T="$CC_TMP/hdeps"
rm -rf "$T"
mkdir -p "$T/usr.bin"
cp "$ROOT/Makefile.inc" "$T/"
[ -f "$ROOT/Makefile.bin.inc" ] && cp "$ROOT/Makefile.bin.inc" "$T/"
cp -r "$ROOT/usr.bin/cc" "$T/usr.bin/cc"
make -s -C "$T/usr.bin/cc" clean >/dev/null 2>&1 || true
jobs=$(nproc 2>/dev/null || echo 4)
make -s -C "$T/usr.bin/cc" -j"$jobs" NATIVE_BUILD=1 CC=gcc cc >/dev/null 2>&1

if [ ! -f "$T/usr.bin/cc/frontend/preproc.d" ]; then
	echo "run_native_header_deps: a host build wrote no .d file for frontend/preproc.o"
	exit 1
fi
pre=$(stat -c %Y "$T/usr.bin/cc/frontend/preproc.o")
lex=$(stat -c %Y "$T/usr.bin/cc/frontend/lexer.o")
sleep 1
# cc_options.h: preproc.c includes it, lexer.c does not
if grep -q 'cc_options.h' "$T/usr.bin/cc/frontend/lexer.c"; then
	echo "run_native_header_deps: lexer.c includes cc_options.h now; pick another bystander"
	exit 1
fi
touch "$T/usr.bin/cc/include/cc_options.h"
make -s -C "$T/usr.bin/cc" -j"$jobs" NATIVE_BUILD=1 CC=gcc cc >/dev/null 2>&1
if [ "$(stat -c %Y "$T/usr.bin/cc/frontend/preproc.o")" = "$pre" ]; then
	echo "run_native_header_deps: touching cc_options.h did not rebuild frontend/preproc.o, which includes it"
	exit 1
fi
if [ "$(stat -c %Y "$T/usr.bin/cc/frontend/lexer.o")" != "$lex" ]; then
	echo "run_native_header_deps: touching cc_options.h rebuilt frontend/lexer.o, which does not include it"
	exit 1
fi
echo "run_native_header_deps: ok"
