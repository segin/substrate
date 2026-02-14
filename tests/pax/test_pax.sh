#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PAX="$ROOT/usr.bin/pax/pax"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; exit 1; }

build_pax() {
  make -C "$ROOT/usr.bin/pax" NATIVE_BUILD=1 >/dev/null
  [ -x "$PAX" ] || fail "build"
}

roundtrip_rw() {
  mkdir -p "$TMP/src/sub"
  printf 'hello\n' > "$TMP/src/sub/a.txt"
  "$PAX" -w -x pax -f "$TMP/a.pax" "$TMP/src"
  mkdir "$TMP/out"
  (cd "$TMP/out" && "$PAX" -r -f "$TMP/a.pax")
  cmp "$TMP/src/sub/a.txt" "$TMP/out/$TMP/src/sub/a.txt"
  pass "roundtrip_rw"
}

interop_tar_to_pax() {
  command -v tar >/dev/null || { echo "SKIP interop_tar_to_pax (tar missing)"; return 0; }
  mkdir -p "$TMP/tar_src"
  printf 'tarfile' > "$TMP/tar_src/f"
  tar -cf "$TMP/in.tar" -C "$TMP" tar_src
  mkdir -p "$TMP/tar_unpack"
  (cd "$TMP/tar_unpack" && "$PAX" -r -f "$TMP/in.tar")
  "$PAX" -w -x pax -f "$TMP/out_from_tar.pax" "$TMP/tar_unpack"
  [ -s "$TMP/out_from_tar.pax" ] || fail "interop_tar_to_pax"
  pass "interop_tar_to_pax"
}

interop_cpio_to_pax() {
  command -v cpio >/dev/null || { echo "SKIP interop_cpio_to_pax (cpio missing)"; return 0; }
  mkdir -p "$TMP/cpio_src"
  printf 'cpiofile' > "$TMP/cpio_src/f"
  (cd "$TMP" && printf 'cpio_src\n' | cpio -o -H newc > "$TMP/in.cpio" 2>/dev/null)
  mkdir -p "$TMP/cpio_unpack"
  (cd "$TMP/cpio_unpack" && "$PAX" -r -f "$TMP/in.cpio")
  "$PAX" -w -x pax -f "$TMP/out_from_cpio.pax" "$TMP/cpio_unpack"
  [ -s "$TMP/out_from_cpio.pax" ] || fail "interop_cpio_to_pax"
  pass "interop_cpio_to_pax"
}

subst_order() {
  mkdir -p "$TMP/subst"
  printf x > "$TMP/subst/alpha.txt"
  "$PAX" -w -x pax -s '/alpha/beta/' -s '/beta/gamma/' -f "$TMP/subst.pax" "$TMP/subst/alpha.txt"
  mkdir "$TMP/subst_out"
  (cd "$TMP/subst_out" && "$PAX" -r -f "$TMP/subst.pax")
  [ -f "$TMP/subst_out/$TMP/subst/gamma.txt" ] || fail "subst_order"
  pass "subst_order"
}

safe_extract() {
  mkdir -p "$TMP/safe"
  printf safe > "$TMP/safe/x"
  "$PAX" -w -x pax -f "$TMP/safe.pax" "$TMP/safe/x"
  mkdir "$TMP/safe_out"
  (cd "$TMP/safe_out" && "$PAX" -r -f "$TMP/safe.pax")
  [ -f "$TMP/safe_out/$TMP/safe/x" ] || fail "safe_extract"
  pass "safe_extract"
}

copy_mode() {
  mkdir -p "$TMP/copy/src"
  printf cpy > "$TMP/copy/src/f"
  mkdir -p "$TMP/copy/dst"
  "$PAX" -r -w "$TMP/copy/src/f" "$TMP/copy/dst"
  [ -f "$TMP/copy/dst/f" ] || fail "copy_mode"
  pass "copy_mode"
}

pax_extended_header_roundtrip() {
  longdir="$TMP/$(printf 'a%.0s' {1..120})"
  mkdir -p "$longdir"
  printf loooooong > "$longdir/f"
  "$PAX" -w -x pax -f "$TMP/long.pax" "$longdir/f"
  strings "$TMP/long.pax" | grep -q 'path=' || fail "pax_extended_header_roundtrip"
  pass "pax_extended_header_roundtrip"
}

build_pax
roundtrip_rw
interop_tar_to_pax
interop_cpio_to_pax
subst_order
safe_extract
copy_mode
pax_extended_header_roundtrip

echo "All pax tests passed"
