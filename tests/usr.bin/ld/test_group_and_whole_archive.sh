#!/bin/sh
set -eu

# Reqs: LD-S-001, LD-U-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-group-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

mkdir -p "$TMP/liba" "$TMP/libb" "$TMP/libwhole"

cat > "$TMP/start_ref.c" <<'SRC'
int a(void);
int start_ref(void) { return a(); }
SRC
gcc -m64 -c -o "$TMP/start_ref.o" "$TMP/start_ref.c"

cat > "$TMP/a.c" <<'SRC'
int b(void);
int a(void) { return b(); }
SRC
gcc -m64 -c -o "$TMP/a.o" "$TMP/a.c"
ar rcs "$TMP/liba/liba.a" "$TMP/a.o"

cat > "$TMP/b.c" <<'SRC'
int b(void) { return 123; }
SRC
gcc -m64 -c -o "$TMP/b.o" "$TMP/b.c"
ar rcs "$TMP/libb/libb.a" "$TMP/b.o"

"$LDX" -m64 -r -L"$TMP/libb" -L"$TMP/liba" -o "$TMP/out_nogroup.o" "$TMP/start_ref.o" -lb -la
if ! nm "$TMP/out_nogroup.o" | grep -q " U b"; then
	echo "FAIL: expected unresolved b without --start-group/--end-group" >&2
	exit 1
fi

"$LDX" -m64 -r -L"$TMP/libb" -L"$TMP/liba" -o "$TMP/out_group.o" "$TMP/start_ref.o" --start-group -lb -la --end-group
if nm "$TMP/out_group.o" | grep -q " U b"; then
	echo "FAIL: expected group fixpoint extraction to resolve b" >&2
	exit 1
fi
if ! nm "$TMP/out_group.o" | grep -q " b$"; then
	echo "FAIL: grouped link did not extract provider symbol b" >&2
	exit 1
fi

cat > "$TMP/whole.c" <<'SRC'
int whole_only(void) { return 77; }
SRC
gcc -m64 -c -o "$TMP/whole.o" "$TMP/whole.c"
ar rcs "$TMP/libwhole/libwhole.a" "$TMP/whole.o"

"$LDX" -m64 -r -L"$TMP/libwhole" -o "$TMP/out_no_whole.o" "$TMP/start_ref.o" -lwhole
if nm "$TMP/out_no_whole.o" | grep -q "whole_only"; then
	echo "FAIL: archive member unexpectedly extracted without --whole-archive" >&2
	exit 1
fi

"$LDX" -m64 -r -L"$TMP/libwhole" -o "$TMP/out_whole.o" "$TMP/start_ref.o" --whole-archive -lwhole --no-whole-archive
if ! nm "$TMP/out_whole.o" | grep -q "whole_only"; then
	echo "FAIL: --whole-archive did not force extraction" >&2
	exit 1
fi

if "$LDX" -m64 -r -L"$TMP/libb" -o "$TMP/out_bad_end.o" "$TMP/start_ref.o" --end-group -lb >"$TMP/end.err" 2>&1; then
	echo "FAIL: unmatched --end-group unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "without matching --start-group" "$TMP/end.err"; then
	echo "FAIL: missing unmatched --end-group diagnostic" >&2
	cat "$TMP/end.err" >&2
	exit 1
fi

if "$LDX" -m64 -r -L"$TMP/libb" -o "$TMP/out_bad_start.o" "$TMP/start_ref.o" --start-group -lb >"$TMP/start.err" 2>&1; then
	echo "FAIL: unmatched --start-group unexpectedly succeeded" >&2
	exit 1
fi
if ! grep -q "without matching --end-group" "$TMP/start.err"; then
	echo "FAIL: missing unmatched --start-group diagnostic" >&2
	cat "$TMP/start.err" >&2
	exit 1
fi

echo "ok: ld --start-group/--end-group and --whole-archive semantics"
