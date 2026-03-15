#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

printf '00:01.0 1234:5678 class=0101 irq=14\n' > "$TMPDIR/pci"
printf 'pci\n  pci00:01.0\nisa\n  serial0\n' > "$TMPDIR/devtree"

make -C "$ROOT/bin/lspci" NATIVE_BUILD=1 >/dev/null
make -C "$ROOT/bin/devtree" NATIVE_BUILD=1 >/dev/null

"$ROOT/bin/lspci/lspci" "$TMPDIR/pci" > "$TMPDIR/pci.out"
"$ROOT/bin/devtree/devtree" "$TMPDIR/devtree" > "$TMPDIR/devtree.out"

diff -u "$TMPDIR/pci" "$TMPDIR/pci.out"
diff -u "$TMPDIR/devtree" "$TMPDIR/devtree.out"
