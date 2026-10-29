#!/bin/sh
# run_codegen_old_gcc.sh: run_codegen_diff.sh, judged by a gcc older than
# a test needs, skips that test and does not fail it.
#
# CI's gcc was older than the one the codegen tests are written against.
# It could not build the C23 and _BitInt tests ("gcc cannot build it"),
# none of them cc's failures.  A test names the gcc it needs in "cc-gcc:",
# for -m32 and -m64 alike: gcc 14 brought _BitInt to both.
# run_codegen_abi.sh leaves out its _BitInt cases for such a gcc.
#
# The old gcc here is this host's, saying it is gcc 13 and refusing what
# gcc 13 refuses: -std=c23, and _BitInt; and saying it is gcc 14, which
# refuses neither.
set -u

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cc_old_gcc.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
fail=0

if ! gcc -v 2>&1 | grep -q '^gcc version'; then
	echo "run_codegen_old_gcc: skipped (no gcc to make an old one of)"
	exit 0
fi

# gcc 13 says C2X is 202000L, so a "__STDC_VERSION__ >= 202311L" block is
# not compiled; what is left must not use _BitInt.
cat > "$tmp/gcc13" <<'EOF'
#!/bin/sh
[ "$*" = -dumpversion ] && { echo 13.3.0; exit 0; }
[ "$*" = -v ] && { echo "gcc version 13.3.0 (pretended)" >&2; exit 0; }
cpp=
for a; do
	case $a in
	-std=c23 | -std=gnu23) echo "gcc13: unrecognized command-line option '$a'" >&2; exit 1 ;;
	-std=c2x | -std=gnu2x) cpp="$cpp $a -U__STDC_VERSION__ -D__STDC_VERSION__=202000L" ;;
	-std=* | -m32 | -m64 | -D* | -U* | -I*) cpp="$cpp $a" ;;
	esac
done
for a; do
	case $a in
	*.c) if gcc -E $cpp "$a" 2>/dev/null | grep -q _BitInt; then echo "gcc13: _BitInt is not supported" >&2; exit 1; fi ;;
	esac
done
case " $cpp " in *" -std=gnu2x "* | *" -std=c2x "*) exec gcc "$@" -U__STDC_VERSION__ -D__STDC_VERSION__=202000L ;; esac
exec gcc "$@"
EOF
chmod +x "$tmp/gcc13"

# With the old gcc: what needs a newer one is skipped, and no run fails.
# (alignof_object names no gcc, and is still run.)
HOST_CC=$tmp/gcc13 sh "$ROOT/tests/usr.bin/cc/run_codegen_diff.sh" \
	nullptr_type bitint_narrow float16_data alignof_object > "$tmp/old.out" 2>&1
rc=$?
if [ $rc -ne 0 ]; then
	echo "FAIL: with gcc 13 the runner failed:"; sed 's/^/    /' "$tmp/old.out"; fail=1
fi
for want in "skip nullptr_type -m64: needs gcc 14" "skip bitint_narrow -m64: needs gcc 14" \
	"skip bitint_narrow -m32: needs gcc 14"; do
	grep -q "^$want" "$tmp/old.out" || { echo "FAIL: no '$want'"; fail=1; }
done
# float16_data needs an ISA, not a newer gcc, and alignof_object nothing at all.
if grep -qE '^skip (float16_data|alignof_object)' "$tmp/old.out"; then
	echo "FAIL: a run that needs no newer gcc was skipped:"; grep '^skip' "$tmp/old.out" | sed 's/^/    /'; fail=1
fi

# gcc 14 has _BitInt for -m32 too: nothing is skipped.
cat > "$tmp/gcc14" <<'EOF'
#!/bin/sh
[ "$*" = -dumpversion ] && { echo 14.2.0; exit 0; }
[ "$*" = -v ] && { echo "gcc version 14.2.0 (pretended)" >&2; exit 0; }
exec gcc "$@"
EOF
chmod +x "$tmp/gcc14"
HOST_CC=$tmp/gcc14 sh "$ROOT/tests/usr.bin/cc/run_codegen_diff.sh" bitint_narrow nullptr_type > "$tmp/g14.out" 2>&1 ||
	{ echo "FAIL: with gcc 14 the runner failed:"; sed 's/^/    /' "$tmp/g14.out"; fail=1; }
if grep -q '^skip' "$tmp/g14.out"; then
	echo "FAIL: gcc 14 skipped:"; grep '^skip' "$tmp/g14.out" | sed 's/^/    /'; fail=1
fi

# With this host's own gcc nothing is skipped -- when it is new enough.
major=$(gcc -dumpversion | cut -d. -f1)
if [ "$major" -ge 16 ]; then
	sh "$ROOT/tests/usr.bin/cc/run_codegen_diff.sh" nullptr_type bitint_narrow float16_data > "$tmp/new.out" 2>&1 ||
		{ echo "FAIL: with gcc $major the runner failed:"; sed 's/^/    /' "$tmp/new.out"; fail=1; }
	if grep -q '^skip' "$tmp/new.out"; then
		echo "FAIL: gcc $major skipped:"; grep '^skip' "$tmp/new.out" | sed 's/^/    /'; fail=1
	fi
fi

# run_codegen_abi.sh with the old gcc: no _BitInt in either half, and no failure.
HOST_CC=$tmp/gcc13 sh "$ROOT/tests/usr.bin/cc/run_codegen_abi.sh" > "$tmp/abi.out" 2>&1 ||
	{ echo "FAIL: with gcc 13 run_codegen_abi failed:"; sed 's/^/    /' "$tmp/abi.out"; fail=1; }
grep -q '^run_codegen_abi: -m64: .*gcc13 has no _BitInt; its cases are skipped' "$tmp/abi.out" ||
	{ echo "FAIL: run_codegen_abi did not skip the _BitInt cases for gcc 13"; fail=1; }

[ $fail -eq 0 ] && echo "run_codegen_old_gcc: ok"
exit $fail
