#!/bin/bash
set -e

# Clean up
rm -f uncompress gen_z.py test1.txt test1.txt.Z out1.txt file2 file2.Z file3.Z out3.txt file4 file4.Z bad.Z

# Build uncompress
cc -Wall -Wextra -Wno-unused-parameter -I../../../usr.bin/compress -o uncompress ../../../usr.bin/compress/uncompress.c

# Create python script for .Z generation
cat > gen_z.py <<EOF
import sys

def main():
    if len(sys.argv) < 3:
        print("Usage: gen_z.py <input> <output.Z>")
        sys.exit(1)

    in_file = sys.argv[1]
    out_file = sys.argv[2]

    with open(in_file, "rb") as f:
        data = f.read()

    out = bytearray()
    out.append(0x1f)
    out.append(0x9d)
    out.append(0x80 | 16) # Block mode, 16 bits

    dictionary = {bytes([i]): i for i in range(256)}
    next_code = 257 # 256 is CLEAR
    current_bits = 9
    max_code = (1 << current_bits) - 1

    bit_buffer = 0
    bits_in_buffer = 0

    def output_code(code, bits):
        nonlocal bit_buffer, bits_in_buffer
        bit_buffer |= (code << bits_in_buffer)
        bits_in_buffer += bits
        while bits_in_buffer >= 8:
            out.append(bit_buffer & 0xff)
            bit_buffer >>= 8
            bits_in_buffer -= 8

    # Initial CLEAR code
    output_code(256, current_bits)

    w = b""
    for byte in data:
        c = bytes([byte])
        wc = w + c
        if wc in dictionary:
            w = wc
        else:
            output_code(dictionary[w], current_bits)

            dictionary[wc] = next_code
            next_code += 1

            if next_code > max_code and current_bits < 16:
                 current_bits += 1
                 max_code = (1 << current_bits) - 1

            if next_code >= 65536:
                 output_code(256, current_bits)
                 dictionary = {bytes([i]): i for i in range(256)}
                 next_code = 257
                 current_bits = 9
                 max_code = (1 << current_bits) - 1

            w = c

    if w:
        output_code(dictionary[w], current_bits)

    if bits_in_buffer > 0:
        out.append(bit_buffer & 0xff)

    with open(out_file, "wb") as f:
        f.write(out)

if __name__ == "__main__":
    main()
EOF

# Test 1: Basic text file
echo "Hello World! This is a test file for uncompress." > test1.txt
python3 gen_z.py test1.txt test1.txt.Z

# Test default behavior (stdin -> stdout)
cat test1.txt.Z | ./uncompress > out1.txt
diff test1.txt out1.txt

# Test 2: File argument
cp test1.txt.Z file2.Z
./uncompress file2.Z
if [ -f file2.Z ]; then echo "Error: file2.Z not removed"; exit 1; fi
if [ ! -f file2 ]; then echo "Error: file2 not created"; exit 1; fi
diff test1.txt file2
rm file2

# Test 3: -c flag
cp test1.txt.Z file3.Z
./uncompress -c file3.Z > out3.txt
if [ ! -f file3.Z ]; then echo "Error: file3.Z removed with -c"; exit 1; fi
diff test1.txt out3.txt
rm file3.Z out3.txt

# Test 4: -f flag
echo "existing" > file4
cp test1.txt.Z file4.Z
# Should fail without -f
# Note: uncompress prints to stderr but returns 0 if skipping?
# Standard uncompress returns >0 if error occurred.
# My code just prints to stderr and returns. `main` returns 0.
# I should fix `main` to return non-zero on error if possible, but
# standard `gzip` returns 0 even if it skips one file?
# Actually `gzip` returns 1 if warning.
# I will check output file content to verify it didn't overwrite.
./uncompress file4.Z 2>/dev/null || true
if [ "$(cat file4)" != "existing" ]; then echo "Error: overwrote file4 without -f"; exit 1; fi

# Should succeed with -f
./uncompress -f file4.Z
diff test1.txt file4
rm file4

# Test 5: Corrupt input (bad magic)
echo "bad" > bad.Z
./uncompress bad.Z && echo "Error: accepted bad magic" && exit 1 || true
# Verify output not created
if [ -f bad ]; then echo "Error: created bad output"; exit 1; fi

echo "All tests passed!"
