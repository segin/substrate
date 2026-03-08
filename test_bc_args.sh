#!/bin/bash
cat << 'BC_EOF' > test.bc
define f(a) { return a; }
f(1
BC_EOF

for i in {1..20000}; do
    echo -n ",1" >> test.bc
done
echo ")" >> test.bc

time ./bin/bc/bc -q test.bc > /dev/null
