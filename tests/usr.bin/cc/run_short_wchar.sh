#!/bin/sh
# -fshort-wchar makes wchar_t unsigned short, as gcc does: an L"..."
# literal's elements are 2 bytes.  Linux builds with it, and its EFI
# code names variables in UTF-16 -- `efi_char16_t name[] = L"db"`,
# get_cert_list(L"db", ...) -- which cc, taking the option and ignoring
# it, refused: "cannot convert initializer 0 for array".  The program
# -- an unsigned short array from L"...", one passed on, sizeof,
# wchar_t's macros and limits, and the type of a character constant
# L'a' (wchar_t), u'a' (char16_t) and U'a' (char32_t), which cc made int
# whatever its prefix -- prints what gcc's does with the option and
# without, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_short_wchar: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stddef.h>
#include <stdio.h>

#if __SIZEOF_WCHAR_T__ == 2
typedef unsigned short efi_char16_t;
static const efi_char16_t db[] = L"db\x7f\xffSecureBoot";

__attribute__((noinline)) int units(const efi_char16_t *s)
{
	int n = 0, sum = 0;

	while (s[n] != 0) {
		sum = sum * 7 + s[n++];
	}
	return sum + n;
}
#endif

int main(void)
{
	wchar_t w = L'\xffff';

	printf("%d %d %d %ld\n", (int)sizeof(L"ab"), (int)sizeof(wchar_t), (int)sizeof(L'a'), (long)w);
	printf("%d %d %d %d\n", (int)sizeof(u'a'), (int)sizeof(U'a'), u'\xffff' > 0, L'\xffff' > 0);
	printf("%d %lld %lld\n", __SIZEOF_WCHAR_T__, (long long)__WCHAR_MAX__, (long long)__WCHAR_MIN__);
#if __SIZEOF_WCHAR_T__ == 2
	printf("%d %d %d\n", (int)sizeof(db), units(db), units(L"PK"));
#endif
	return 0;
}
EOF2
for m in 64 32; do
	for f in -fshort-wchar "" "-fshort-wchar -fno-short-wchar"; do
		# shellcheck disable=SC2086
		gcc -m$m -O1 $f -w -o "$CC_TMP/g" "$CC_TMP/b.c" 2>/dev/null || continue
		want=$("$CC_TMP/g")
		for o in O0 O2; do
			for path in mir legacy; do
				run="env"
				if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
				# shellcheck disable=SC2086
				if ! $run "$CC_BIN" -m$m -$o $f -o "$CC_TMP/c" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
					echo "run_short_wchar: -m$m -$o $path [$f]: $(head -1 "$CC_TMP/c.err")"
					exit 1
				fi
				got=$("$CC_TMP/c")
				if [ "$got" != "$want" ]; then
					echo "run_short_wchar: -m$m -$o $path [$f] prints [$got], gcc's [$want]"
					exit 1
				fi
			done
		done
	done
done
echo "run_short_wchar: ok"
