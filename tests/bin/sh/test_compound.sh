
# Test Compound Redirections
rm -f parser.o exec.o sh
make -C bin/sh sh NATIVE_BUILD=1
./bin/sh/sh test_compound.sh
