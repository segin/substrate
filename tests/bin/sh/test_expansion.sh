#!/bin/sh

echo "--- Testing Field Splitting ---"
IFS=" :"
VAR="a b:c  d"
# Should be: a, b, c, d
for i in $VAR; do
    echo "F: [$i]"
done

echo "--- Testing Combined Quoted/Unquoted ---"
A="a b"
B="c d"
for i in $A"$B"; do
    echo "C: [$i]"
done

echo "--- Testing Globbing ---"
# Create some test files
touch test_glob_1.txt test_glob_2.txt test_glob_abc.txt
echo "Unquoted glob:"
for i in test_glob_*.txt; do
    echo "G: [$i]"
done

echo "Quoted glob (should not expand):"
for i in "test_glob_*.txt"; do
    echo "Q: [$i]"
done

echo "--- Testing Tilde ---"
echo "Home: $HOME"
echo "Tilde: ~"

echo "--- Testing Arithmetic Splitting ---"
VAR=$((1 + 1))" "$((2 + 2))
for i in $VAR; do
    echo "A: [$i]"
done

# Cleanup
rm test_glob_1.txt test_glob_2.txt test_glob_abc.txt
