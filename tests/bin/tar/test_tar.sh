#!/usr/bin/env bash
set -euo pipefail

TAR_BIN=${1:-tar}
ROOT=$(pwd)/tmp
rm -rf "$ROOT"
mkdir -p "$ROOT"

pass() { echo "[PASS] $1"; }

run_cmd() { "$@" >/dev/null 2>&1; }

test_roundtrip_stream() {
  mkdir -p "$ROOT/src/dir"
  printf 'hello\n' > "$ROOT/src/dir/a.txt"
  printf 'world\n' > "$ROOT/src/b.txt"

  mkdir -p "$ROOT/out"
  (cd "$ROOT" && "$TAR_BIN" -cf - src | (cd out && "$TAR_BIN" -xf -))

  diff -u "$ROOT/src/dir/a.txt" "$ROOT/out/src/dir/a.txt"
  diff -u "$ROOT/src/b.txt" "$ROOT/out/src/b.txt"
  pass roundtrip_stream
}

test_pax_interop() {
  mkdir -p "$ROOT/pax"
  longname="$ROOT/pax/$(printf 'long%.0s' {1..40}).txt"
  mkdir -p "$(dirname "$longname")"
  printf 'pax-data' > "$longname"

  "$TAR_BIN" --format=pax -cf "$ROOT/pax.tar" -C "$ROOT" pax
  tar -tf "$ROOT/pax.tar" | grep -q 'pax/'

  mkdir -p "$ROOT/pax_extract"
  (cd "$ROOT/pax_extract" && tar -xf "$ROOT/pax.tar")
  pass pax_interop
}

test_safe_extract() {
  mkdir -p "$ROOT/unsafe_src"
  printf 'x' > "$ROOT/unsafe_src/file"
  (cd "$ROOT/unsafe_src" && tar -cf "$ROOT/unsafe.tar" file)

  python3 - <<'PY'
import tarfile
with tarfile.open('tmp/unsafe2.tar','w') as t:
    ti=tarfile.TarInfo('../escape.txt')
    data=b'boom'
    ti.size=len(data)
    t.addfile(ti, fileobj=__import__('io').BytesIO(data))
PY

  mkdir -p "$ROOT/safe_out"
  set +e
  (cd "$ROOT/safe_out" && "$TAR_BIN" -xf "$ROOT/unsafe2.tar")
  rc=$?
  set -e
  [[ $rc -eq 0 ]]
  [[ ! -f "$ROOT/escape.txt" ]]
  pass safe_extract
}

test_incremental() {
  mkdir -p "$ROOT/inc/src"
  printf 'v1' > "$ROOT/inc/src/a"
  "$TAR_BIN" -cf "$ROOT/inc/base.tar" --listed-incremental="$ROOT/inc/snap" -C "$ROOT/inc" src
  sleep 1
  printf 'v2' > "$ROOT/inc/src/a"
  printf 'new' > "$ROOT/inc/src/b"
  "$TAR_BIN" -uf "$ROOT/inc/base.tar" --listed-incremental="$ROOT/inc/snap" -C "$ROOT/inc" src
  tar -tf "$ROOT/inc/base.tar" | grep -q 'src/b'
  pass incremental
}

test_sparse_roundtrip() {
  mkdir -p "$ROOT/sparse"
  truncate -s 16M "$ROOT/sparse/orig"
  dd if=/dev/zero bs=1 count=0 seek=0 of="$ROOT/sparse/orig" 2>/dev/null
  printf 'end' | dd of="$ROOT/sparse/orig" bs=1 seek=$((16*1024*1024-3)) conv=notrunc 2>/dev/null

  "$TAR_BIN" -cf "$ROOT/sparse.tar" -C "$ROOT" sparse
  mkdir -p "$ROOT/sparse_out"
  (cd "$ROOT/sparse_out" && "$TAR_BIN" -xf "$ROOT/sparse.tar")

  [[ $(stat -c %s "$ROOT/sparse_out/sparse/orig") -eq $(stat -c %s "$ROOT/sparse/orig") ]]
  pass sparse_roundtrip
}

test_roundtrip_stream
test_pax_interop
test_safe_extract
test_incremental
test_sparse_roundtrip

echo "all tar integration tests passed"
