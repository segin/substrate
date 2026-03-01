#!/bin/sh
set -eu

TOP=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
READELF="$TOP/usr.bin/readelf/readelf"
TMP=${TMPDIR:-/tmp}/substrate-readelf-test-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

make -C "$TOP/usr.bin/readelf" NATIVE_BUILD=1 >/dev/null

check() {
	"$@"
}

check "$READELF" -h /bin/ls | grep -q "ELF Header"
check "$READELF" -S /bin/ls | grep -q "Section Headers"
check "$READELF" -l /bin/ls | grep -q "Program Headers"
check "$READELF" -s /bin/ls | grep -q "Symbol table"
check "$READELF" -r /bin/ls | grep -q "Relocation section"
check "$READELF" -d /bin/ls | grep -q "Dynamic section"
check "$READELF" -n /bin/ls | grep -q "Displaying notes found"
check "$READELF" -V /bin/ls | grep -q "Version symbols section"
check "$READELF" -I /bin/ls | grep -E -q "(GNU hash table|SYSV hash table)"
check "$READELF" -x .interp -p .interp /bin/ls | grep -q "String dump"

check "$READELF" --dyn-syms /bin/ls | grep -q ".dynsym"
check "$READELF" --sym-base=10 -D -s /bin/ls | grep -q "Symbol table"

printf "not-elf\n" >"$TMP/notelf.bin"
if "$READELF" -h "$TMP/notelf.bin" >/dev/null 2>&1; then
	echo "expected non-ELF failure"
	exit 1
fi

dd if=/bin/ls of="$TMP/trunc.bin" bs=1 count=80 >/dev/null 2>&1
"$READELF" -h "$TMP/trunc.bin" >/dev/null 2>&1 || true

echo "readelf smoke tests passed"
