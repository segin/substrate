#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPIO="$ROOT/cpio"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Setup Attacker
mkdir -p "$TMP/attacker/subdir"
echo "pwned" > "$TMP/attacker/target"
# Create archive with relative path traversal
(cd "$TMP/attacker/subdir" && echo "../target" | "$CPIO" -o -H newc > "$TMP/exploit.cpio" 2>/dev/null)

# Setup Victim
mkdir -p "$TMP/victim/subdir"
echo "original" > "$TMP/victim/target"

# Extract
# If vulnerable, "../target" is overwritten with "pwned"
(cd "$TMP/victim/subdir" && "$CPIO" -i < "$TMP/exploit.cpio" >/dev/null 2>&1 || true)

# Check
if grep -q "pwned" "$TMP/victim/target"; then
    echo "FAIL: File was overwritten! Vulnerability confirmed."
    exit 1
else
    echo "PASS: File was protected."
    exit 0
fi
