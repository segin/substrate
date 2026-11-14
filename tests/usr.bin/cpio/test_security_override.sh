#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CPIO=${CPIO:-"$ROOT/usr.bin/cpio/cpio"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Setup Attacker
mkdir -p "$TMP/attacker/subdir"
echo "pwned" > "$TMP/attacker/target"
# Create archive
(cd "$TMP/attacker/subdir" && echo "../target" | "$CPIO" -o -H newc > "$TMP/exploit.cpio" 2>/dev/null)

# Setup Victim
mkdir -p "$TMP/victim/subdir"
echo "original" > "$TMP/victim/target"

# Extract with --insecure (and -u: the victim file is the newer one)
# Should overwrite
(cd "$TMP/victim/subdir" && "$CPIO" -iu --insecure < "$TMP/exploit.cpio" >/dev/null 2>&1 || true)

# Check
if grep -q "pwned" "$TMP/victim/target"; then
    echo "PASS: File was overwritten (as expected with --insecure)."
    exit 0
else
    echo "FAIL: File was NOT overwritten (despite --insecure)."
    exit 1
fi
