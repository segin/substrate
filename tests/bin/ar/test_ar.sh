#!/bin/bash
set -e

# Get absolute path to repo root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"

AR_SRC="$REPO_ROOT/usr.bin/ar/ar.c"
AR_DIR="$REPO_ROOT/usr.bin/ar"
ELF_INC="$REPO_ROOT/sys/exec/formats"

# Create temp dir
TEST_DIR="$SCRIPT_DIR/tmp_ar_test"
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

echo "Compiling ar..."
# Check if -m32 works with headers
echo "#include <sys/types.h>" > check.c
if gcc -m32 -c check.c >/dev/null 2>&1; then
    CC="gcc -m32"
else
    echo "32-bit headers missing, falling back to 64-bit host compiler."
    CC="gcc"
fi
rm -f check.c check.o

$CC -I"$ELF_INC" -I"$AR_DIR" -o ar "$AR_SRC"

echo "Creating dummy object file..."
cat > foo.c <<EOF
int foo_func(void) { return 42; }
EOF

if gcc -m32 -c foo.c -o foo.o >/dev/null 2>&1; then
    echo "Generated 32-bit object."
else
    echo "Cannot generate 32-bit object. Trying default (maybe 64-bit)."
    $CC -c foo.c -o foo.o
fi

echo "Testing 'ar rc'..."
./ar rc libfoo.a foo.o
if [ ! -f libfoo.a ]; then
    echo "Archive not created."
    exit 1
fi

echo "Testing 'ar t'..."
if ./ar t libfoo.a | grep -q "foo.o"; then
    echo "Member found."
else
    echo "Member not found."
    exit 1
fi

echo "Testing 'ar x'..."
rm foo.o
./ar x libfoo.a
if [ -f foo.o ]; then
    echo "Extraction successful."
else
    echo "Extraction failed."
    exit 1
fi

echo "Testing 'ar s' (ranlib)..."
./ar s libfoo.a
if grep -a "__.SYMDEF" libfoo.a >/dev/null; then
    echo "Symbol table found."
else
    echo "Symbol table missing."
    exit 1
fi

echo "Testing symbol extraction..."
if grep -a "foo_func" libfoo.a >/dev/null; then
    echo "Symbol 'foo_func' found in archive."
else
    echo "Symbol 'foo_func' NOT found in archive."
    exit 1
fi

echo "Testing 'ranlib' invocation..."
ln -sf ar ranlib
./ranlib libfoo.a
if grep -a "__.SYMDEF" libfoo.a >/dev/null; then
    echo "Ranlib worked."
else
    echo "Ranlib failed."
fi

echo "Testing BSD long filenames..."
LONGNAME="this_is_a_very_long_filename_that_exceeds_16_chars.o"
cp foo.o "$LONGNAME"
./ar rc liblong.a "$LONGNAME"
if ./ar t liblong.a | grep -q "$LONGNAME"; then
    echo "Long filename supported."
else
    echo "Long filename failed."
    ./ar t liblong.a
    exit 1
fi

# Cleanup
cd ..
rm -rf "$TEST_DIR"

echo "All tests passed."
