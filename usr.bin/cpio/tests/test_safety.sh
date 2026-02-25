#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPIO="$ROOT/cpio"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/in"
printf hi > "$TMP/in/file"
printf '%s\n' ../escape /abs src | (cd "$TMP/in" && "$CPIO" -o -H newc -F "$TMP/a.cpio" || true)
mkdir -p "$TMP/out"
(cd "$TMP/out" && "$CPIO" -i -d --safe-extract -F "$TMP/a.cpio" || true)
test ! -e "$TMP/escape"

# Test default safety (no flags) - should block ../ traversal
mkdir -p "$TMP/vuln_test"
mkdir -p "$TMP/vuln_test/src/subdir"
mkdir -p "$TMP/vuln_test/extract/subdir"

# 1. Create source content
echo "VULNERABLE" > "$TMP/vuln_test/target"

# 2. Archive it as "../../target" relative to current dir
(cd "$TMP/vuln_test/src/subdir" && echo "../../target" | "$CPIO" -o -H newc > "$TMP/vuln_test/exploit.cpio")

# 3. Reset target to SAFE
echo "SAFE" > "$TMP/vuln_test/target"

# 4. Extract with default options
(cd "$TMP/vuln_test/extract/subdir" && "$CPIO" -i < "$TMP/vuln_test/exploit.cpio" 2>/dev/null || true)

# 5. Check if overwritten
CONTENT=$(cat "$TMP/vuln_test/target")
if [ "$CONTENT" = "VULNERABLE" ]; then
    echo "FAIL: Traversal vulnerability exploited (default behavior insecure)!"
    exit 1
else
    echo "PASS: Traversal blocked (default behavior secure)."
fi
