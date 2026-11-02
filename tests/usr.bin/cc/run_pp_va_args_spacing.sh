#!/bin/sh
# The commas between a macro's variable arguments are spelled as the call
# spelled them: gcc's `#__VA_ARGS__` of `p,q , r` is "p,q , r", and a
# comma without white space around it has none.  cc joined the arguments
# with ", " whatever the call had, so Linux's __stringify(KVM_SUB_MODULES)
# of -DKVM_SUB_MODULES=kvm-amd,kvm-intel was "kvm-amd, kvm-intel", and
# modpost read a module namespace of `kvm-amd` and ` kvm-intel` (225
# "does not import it" warnings).  The -E text of each is gcc's, the
# variable arguments stringified and substituted, of __VA_ARGS__ and of a
# named variadic parameter.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_va_args_spacing: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/a.c" <<'EOF2'
#define __stringify_1(x...) #x
#define __stringify(x...) __stringify_1(x)
#define S(...) #__VA_ARGS__
#define T(a, ...) #a #__VA_ARGS__
#define N(args...) #args
#define U(...) [__VA_ARGS__]
s1 __stringify(LIST)
s2 S(p,q , r)
s3 S( a , b )
s4 S(a,(b, c) ,d)
s5 S(a,,b)
s6 S(x ,y)
s7 T(one,two ,three)
s8 N(m,n, o)
s9 U(a,b , c)
s10 __stringify(a,b)
EOF2
gcc -E -P -DLIST=kvm-amd,kvm-intel "$CC_TMP/a.c" | tr -s ' \t' '  ' | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/g.out"
"$CC_BIN" -E -P -DLIST=kvm-amd,kvm-intel -o "$CC_TMP/c.raw" "$CC_TMP/a.c"
tr -s ' \t' '  ' <"$CC_TMP/c.raw" | sed 's/^ //; s/ $//' | grep . >"$CC_TMP/c.out"
if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
	echo "run_pp_va_args_spacing: differs from gcc (cc's on the right):"
	diff "$CC_TMP/g.out" "$CC_TMP/c.out" | grep '^[<>]' | head -20
	exit 1
fi
echo "run_pp_va_args_spacing: ok"
