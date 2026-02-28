CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -g

INCLUDES = -I../../../include
LIBDEMANGLE = ../../../usr.lib/demangle/libdemangle.a

.PHONY: all clean run fuzz-rand fuzz-libfuzzer afl-harness corpus-libstdcxx corpus-llvm libdemangle

all: test_demangle

libdemangle:
	$(MAKE) -C ../../../usr.lib/demangle NATIVE_BUILD=1

test_demangle: test_main.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_main.o $(LIBDEMANGLE)

tools/demangle_cli: tools/demangle_cli.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ tools/demangle_cli.o $(LIBDEMANGLE)

fuzz/fuzz_random: fuzz/fuzz_random.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ fuzz/fuzz_random.o $(LIBDEMANGLE)

fuzz/cxxfilt_stdin_harness: fuzz/cxxfilt_stdin_harness.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ fuzz/cxxfilt_stdin_harness.o $(LIBDEMANGLE)

fuzz/fuzz_demangle_libfuzzer: fuzz/fuzz_demangle_libfuzzer.c libdemangle
	$(CC) -O1 -g -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer $(INCLUDES) -o $@ $< $(LIBDEMANGLE)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: test_demangle
	./test_demangle

fuzz-rand: fuzz/fuzz_random
	./fuzz/fuzz_random

fuzz-libfuzzer: fuzz/fuzz_demangle_libfuzzer
	./fuzz/fuzz_demangle_libfuzzer -runs=20000

afl-harness: fuzz/cxxfilt_stdin_harness
	printf '_Z3foov\n_D3foo3barFiZi\n_RNvCshgxSpmajvKg_7mycrate3foo\n' | ./fuzz/cxxfilt_stdin_harness >/dev/null

corpus-libstdcxx: tools/demangle_cli
	./corpus/run_libstdcxx_compare.sh

corpus-llvm: tools/demangle_cli
	./corpus/run_llvm_corpus.sh

clean:
	rm -f test_demangle test_main.o
	rm -f tools/demangle_cli tools/demangle_cli.o
	rm -f fuzz/fuzz_random fuzz/fuzz_random.o
	rm -f fuzz/cxxfilt_stdin_harness fuzz/cxxfilt_stdin_harness.o
	rm -f fuzz/fuzz_demangle_libfuzzer
