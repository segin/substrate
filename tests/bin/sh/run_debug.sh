#!/bin/bash
set -e
make -C bin/sh sh NATIVE_BUILD=1

# Create a temporary test script that isolates the crash
cat > tests/bin/sh/test_debug_crash.sh << 'EOF'
i=0
while [ $i -lt 1 ]; do
    echo "Loop $i"
    i=$((i+1))
done > out_crash.txt
EOF

echo "Running final multi-line verification..."
./bin/sh/sh tests/bin/sh/test_debug_crash.sh
echo "Test finished."
