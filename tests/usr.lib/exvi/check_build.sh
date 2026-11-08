#!/bin/bash
# The build and interface requirements of the 2026-11-06 exvi audit, each
# as a check that fails if it regresses:
#
#  - with NATIVE_BUILD, bin/ex and bin/vi build against the host's libexvi
#    archive, and that ex runs a pattern: libexvi and the program agree on
#    one regex (a host libexvi linked with usr.lib/regex's larger regex_t
#    smashed the stack on the first pattern)                   EXVI-74, 77
#  - they install every name the manual pages document: ex, rex, vi, view,
#    rvi                                                         EXVI-80
#  - libexvi exports exactly what include/exvi.h declares: exvi.map's
#    global list is the header's functions, and a built libexvi.so.0, if
#    there is one, defines no other dynamic symbol                EXVI-78.1
#  - exvi.h gives C linkage to a C++ caller                      EXVI-78.2
#  - every usr.lib shared object is branded for Substrate, and a built
#    libexvi.so.0, if there is one, carries OS/ABI 0x40           EXVI-79
#  - the ex and vi test targets: stop at the first failure (set -e), run
#    each test on the plain and the ASan build, build into their own
#    directory, and make -C tests runs them and tests/usr.lib/exvi  EXVI-76
#
# The native builds are made in a copy of bin/ex, bin/vi and usr.lib/exvi,
# not in the tree, whose target objects they would replace.
#
# Usage: check_build.sh path/to/tree-top
set -u

TOP="$(cd "${1:?usage: $0 path/to/tree-top}" && pwd)"
T=$(mktemp -d "${TMPDIR:-/tmp}/exvi_build.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# --- the native build, in a copy (EXVI-74, 77, 80)
mkdir -p "$T/top/bin" "$T/top/usr.lib"
cp -r "$TOP/bin/ex" "$TOP/bin/vi" "$T/top/bin/"
cp -r "$TOP/usr.lib/exvi" "$T/top/usr.lib/"
cp "$TOP/Makefile.inc" "$TOP/Makefile.bin.inc" "$T/top/"
ln -s "$TOP/include" "$T/top/include"
ln -s "$TOP/sys" "$T/top/sys"
for d in usr.lib/exvi bin/ex bin/vi; do
	make -s -C "$T/top/$d" clean >/dev/null 2>&1
done
if ! make -s -C "$T/top/usr.lib/exvi" NATIVE_BUILD=1 >"$T/log" 2>&1 ||
   ! make -s -C "$T/top/bin/ex" NATIVE_BUILD=1 >>"$T/log" 2>&1 ||
   ! make -s -C "$T/top/bin/vi" NATIVE_BUILD=1 >>"$T/log" 2>&1; then
	fail "NATIVE_BUILD of libexvi, ex and vi: [$(tail -5 "$T/log")]"
else
	printf 'abc\n' >"$T/f"
	out=$(printf 's/a\\(b\\)/\\1X/\n%%p\nq!\n' | "$T/top/bin/ex/ex" -s "$T/f" 2>&1)
	[ "$out" = bXc ] || fail "the native ex's pattern: [$out]"
	make -s -C "$T/top/bin/ex" NATIVE_BUILD=1 install DESTDIR="$T/dest" >>"$T/log" 2>&1
	make -s -C "$T/top/bin/vi" NATIVE_BUILD=1 install DESTDIR="$T/dest" >>"$T/log" 2>&1
	for name in ex rex vi view rvi; do
		[ -x "$T/dest/bin/$name" ] || fail "make install made no bin/$name"
	done
fi

# --- the interface (EXVI-78)
declared=$(sed -n 's/^[a-z][a-z_ *]*[ *]\(exvi_[a-z_]*\)(.*/\1/p' "$TOP/include/exvi.h" | sort)
exported=$(sed -n '/global:/,/local:/s/^[[:space:]]*\([a-z_]*\);.*/\1/p' "$TOP/usr.lib/exvi/exvi.map" | sort)
if [ -z "$declared" ] || [ "$declared" != "$exported" ]; then
	fail "exvi.map exports [$(echo $exported)], exvi.h declares [$(echo $declared)]"
fi
for so in "$TOP/usr.lib/exvi/libexvi.so.0" "$TOP/usr.lib/exvi/obj-x86_64/libexvi.so.0"; do
	[ -f "$so" ] || continue
	defined=$(nm -D --defined-only "$so" 2>/dev/null | awk '$2 ~ /[TDBR]/ { print $3 }' |
		grep -v '^_' | sort)
	[ "$defined" = "$declared" ] || fail "$so defines [$(echo $defined)]"
	osabi=$(od -An -t x1 -j 7 -N 1 "$so" | tr -d ' ')
	[ "$osabi" = 40 ] || fail "$so has OS/ABI 0x$osabi, not 0x40 (Substrate)"
done
if command -v c++ >/dev/null 2>&1; then
	printf '#include <exvi.h>\nint call(int c, char **v) { return exvi_main(c, v, EXVI_FRONTEND_EX); }\n' >"$T/c.cc"
	if ! c++ -c -I"$TOP/include" -o "$T/c.o" "$T/c.cc" 2>"$T/log"; then
		fail "a C++ file including exvi.h: [$(head -3 "$T/log")]"
	elif ! nm "$T/c.o" | grep -qE ' U exvi_main$'; then
		fail "exvi.h gives exvi_main C++ linkage: [$(nm "$T/c.o" | grep exvi_main)]"
	fi
else
	echo "SKIP: exvi.h from C++ (no c++)"
fi

# --- every usr.lib shared object branded (EXVI-79)
for mk in "$TOP"/usr.lib/*/Makefile; do
	if grep -q 'SHLIB' "$mk" && ! grep -q 'BRAND_SUBSTRATE' "$mk"; then
		fail "${mk#"$TOP"/} builds a shared object and does not brand it"
	fi
done

# --- the test targets (EXVI-76)
for d in ex vi; do
	cmds=$(make -n -C "$TOP/tests/bin/$d" test 2>/dev/null)
	grep -q 'set -e' <<<"$cmds" || fail "tests/bin/$d's test target does not stop at a failure"
	grep -q 'build/asan/' <<<"$cmds" || fail "tests/bin/$d's test target runs no ASan build"
	grep -q 'build/plain/' <<<"$cmds" || fail "tests/bin/$d's test target runs no plain build"
done
grep -q 'CURDIR)/build' "$TOP/tests/bin/exvi-build.mk" ||
	fail "exvi-build.mk does not build into the test directory"
grep -q 'usr.lib/exvi run' "$TOP/tests/bin/ex/Makefile" ||
	fail "the ex test target does not run tests/usr.lib/exvi"
for d in bin/ex bin/vi; do
	grep -qE "MAKE\) -C $d test" "$TOP/tests/Makefile" ||
		fail "make -C tests does not run tests/$d"
done

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: check_build"
