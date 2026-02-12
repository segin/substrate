#!/bin/bash
# Test pax utility

PAX="../../usr.bin/pax/pax"

if [ ! -f "$PAX" ]; then
    echo "pax binary not found at $PAX"
    exit 1
fi

echo "PAX build verified."

if command -v qemu-i386 >/dev/null; then
    echo "Running usage test..."
    qemu-i386 "$PAX" -? 2>&1 | grep "usage"
else
    echo "Skipping runtime tests (qemu-i386 not found)"
fi

exit 0
