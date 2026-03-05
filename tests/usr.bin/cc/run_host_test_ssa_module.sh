#!/bin/sh

DIR="$( cd "$( dirname "$0" )" && pwd )"
cd "$DIR"

echo "Compiling and running test_ssa_module..."

gcc -Wall -Werror -I../../../usr.bin/cc/include \
    test_ssa_module.c \
    ../../../usr.bin/cc/middle/ssa/module.c \
    ../../../usr.bin/cc/middle/ssa/func.c \
    ../../../usr.bin/cc/middle/ssa/instr_set.c \
    ../../../usr.bin/cc/middle/ssa/bblock.c \
    -o test_ssa_module

if [ $? -eq 0 ]; then
    ./test_ssa_module
    RET=$?
    rm test_ssa_module
else
    echo "Compilation failed."
    RET=1
fi
exit $RET
