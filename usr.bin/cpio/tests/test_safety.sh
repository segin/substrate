#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CPIO="$ROOT/cpio"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Create a file outside the extraction root to test traversal
touch "$TMP/outside_file"

mkdir -p "$TMP/in"
# Create archive containing "../outside_file"
# Note: We must create the file so cpio -o can read it, but here we refer to the one we just created
# relative to TMP/in, "../outside_file" is TMP/outside_file.
# But we need to archive it.
# If we run cpio -o in TMP/in, and pass "../outside_file", it reads TMP/outside_file.

(cd "$TMP/in" && printf '../outside_file\n' | "$CPIO" -o -H newc -F "$TMP/attack.cpio" 2>/dev/null)

# Now prepare for extraction
mkdir -p "$TMP/out"
# Remove the outside file to verify if extraction recreates it
rm -f "$TMP/outside_file"

# Extract WITHOUT any safety flags (should be safe by default now)
(cd "$TMP/out" && "$CPIO" -i -d -F "$TMP/attack.cpio" 2>/dev/null || true)

# Check if file was created outside
if [ -e "$TMP/outside_file" ]; then
    echo "FAIL: ../outside_file was created!"
    exit 1
fi

echo "PASS: Path traversal prevented."
