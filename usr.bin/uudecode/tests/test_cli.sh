#!/bin/bash
UUDECODE=$1

if [ -z "$UUDECODE" ]; then
    echo "Usage: $0 <path_to_uudecode>"
    exit 1
fi

echo "Testing $UUDECODE..."

# Create test input
cat > test.uu <<EOF
begin 644 test_output.txt
#0V%T
\`
end
EOF

# Test default decoding
echo "1. Default decoding"
$UUDECODE test.uu
if [ ! -f test_output.txt ]; then
    echo "Failed: test_output.txt not created"
    exit 1
fi
CONTENT=$(cat test_output.txt)
if [ "$CONTENT" != "Cat" ]; then
    echo "Failed: expected 'Cat', got '$CONTENT'"
    exit 1
fi
rm test_output.txt

# Test -p (stdout)
echo "2. -p (stdout)"
OUT=$($UUDECODE -p test.uu)
if [ "$OUT" != "Cat" ]; then
    echo "Failed -p: expected 'Cat', got '$OUT'"
    exit 1
fi
if [ -f test_output.txt ]; then
    echo "Failed -p: file created"
    exit 1
fi

# Test -o (output file)
echo "3. -o (output file)"
$UUDECODE -o specific.txt test.uu
if [ ! -f specific.txt ]; then
    echo "Failed -o: specific.txt not created"
    exit 1
fi
CONTENT=$(cat specific.txt)
if [ "$CONTENT" != "Cat" ]; then
    echo "Failed -o: expected 'Cat', got '$CONTENT'"
    exit 1
fi
rm specific.txt

# Test Path Stripping (secure default)
echo "4. Path Stripping (secure default)"
mkdir -p unsafe_dir
cat > test_unsafe.uu <<EOF
begin 644 unsafe_dir/unsafe.txt
#0V%T
\`
end
EOF

# Default behavior (secure stripping)
echo "   Testing default behavior (strip)..."
$UUDECODE test_unsafe.uu
if [ ! -f unsafe.txt ]; then
    echo "Failed: unsafe.txt not created in CWD (secure default)"
    exit 1
fi
if [ -f unsafe_dir/unsafe.txt ]; then
    echo "Failed: wrote to unsafe_dir/unsafe.txt (insecure default)"
    exit 1
fi
rm unsafe.txt

# With -s (still secure)
echo "   Testing -s behavior (strip)..."
$UUDECODE -s test_unsafe.uu
if [ ! -f unsafe.txt ]; then
    echo "Failed -s: unsafe.txt not created in CWD"
    exit 1
fi
if [ -f unsafe_dir/unsafe.txt ]; then
    echo "Failed -s: wrote to unsafe_dir/unsafe.txt"
    exit 1
fi
rm unsafe.txt
rmdir unsafe_dir
rm test.uu test_unsafe.uu

echo "All CLI tests passed!"
