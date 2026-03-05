#!/bin/bash
set -e

# Create a dummy C file with many symbols
echo "Generating dummy objects..."
mkdir -p bench_objects
cat << 'C_EOF' > bench_objects/gen.py
import sys
for i in range(100):
    with open(f"bench_objects/obj_{i}.c", "w") as f:
        for j in range(10000):
            f.write(f"int dummy_func_really_long_name_just_in_case_{i}_{j}() {{ return {i}+{j}; }}\n")
C_EOF

python3 bench_objects/gen.py
echo "Compiling objects..."
for i in {0..99}; do
    gcc -c bench_objects/obj_$i.c -o bench_objects/obj_$i.o
done

echo "Benchmarking ar..."
for i in {1..5}; do
    time usr.bin/ar/ar rcs bench_objects/libdummy.a bench_objects/*.o
done
