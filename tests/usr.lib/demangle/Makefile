CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -g

INCLUDES = -I../../../include
LIBDEMANGLE = ../../../usr.lib/demangle/libdemangle.a

.PHONY: all clean run fuzz-rand fuzz-libfuzzer fuzz-ci afl-harness corpus-libstdcxx corpus-llvm diff-cxxfilt libdemangle

all: test_demangle test_audit_regressions

libdemangle:
	$(MAKE) -C ../../../usr.lib/demangle NATIVE_BUILD=1

test_demangle: test_main.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_main.o $(LIBDEMANGLE)

test_audit_regressions: test_audit_regressions.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_audit_regressions.o $(LIBDEMANGLE)

tools/demangle_cli: tools/demangle_cli.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ tools/demangle_cli.o $(LIBDEMANGLE)

fuzz/fuzz_random: fuzz/fuzz_random.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ fuzz/fuzz_random.o $(LIBDEMANGLE)

fuzz/cxxfilt_stdin_harness: fuzz/cxxfilt_stdin_harness.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ fuzz/cxxfilt_stdin_harness.o $(LIBDEMANGLE)

fuzz/fuzz_demangle_libfuzzer: fuzz/fuzz_demangle_libfuzzer.c libdemangle
	$(CC) -O1 -g -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer $(INCLUDES) -o $@ $< $(LIBDEMANGLE)

fuzz/diff_cxxfilt: fuzz/diff_cxxfilt.o libdemangle
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ fuzz/diff_cxxfilt.o $(LIBDEMANGLE)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: test_demangle test_audit_regressions
	./test_demangle
	./test_audit_regressions

fuzz-rand: fuzz/fuzz_random
	./fuzz/fuzz_random

fuzz-libfuzzer: fuzz/fuzz_demangle_libfuzzer
	./fuzz/fuzz_demangle_libfuzzer -runs=20000

# CI gate: 60 seconds of dictionary-driven libfuzzing with a fresh
# corpus.  Exits non-zero if any input crashes, hangs, or triggers an
# ASan/UBSan finding.  Designed to be cheap enough to run on every PR.
fuzz-ci: fuzz/fuzz_demangle_libfuzzer
	@mkdir -p fuzz/corpus
	./fuzz/fuzz_demangle_libfuzzer \
	    -dict=fuzz/demangle.dict \
	    -max_total_time=60 \
	    -timeout=10 \
	    -rss_limit_mb=512 \
	    -print_final_stats=1 \
	    fuzz/corpus

# Differential fuzzing against GNU c++filt.  Reads mangled names from
# stdin (one per line); flags inputs where both demanglers succeed but
# produce different output.
diff-cxxfilt: fuzz/diff_cxxfilt
	@if [ ! -t 0 ]; then \
	    ./fuzz/diff_cxxfilt --max-diffs=20; \
	else \
	    echo "Usage: cat corpus.txt | make diff-cxxfilt"; \
	    exit 1; \
	fi

afl-harness: fuzz/cxxfilt_stdin_harness
	printf '_Z3foov\n_D3foo3barFiZi\n_RNvCshgxSpmajvKg_7mycrate3foo\n' | ./fuzz/cxxfilt_stdin_harness >/dev/null

corpus-libstdcxx: tools/demangle_cli
	./corpus/run_libstdcxx_compare.sh

corpus-llvm: tools/demangle_cli
	./corpus/run_llvm_corpus.sh

clean:
	rm -f test_demangle test_main.o
	rm -f test_audit_regressions test_audit_regressions.o
	rm -f tools/demangle_cli tools/demangle_cli.o
	rm -f fuzz/fuzz_random fuzz/fuzz_random.o
	rm -f fuzz/cxxfilt_stdin_harness fuzz/cxxfilt_stdin_harness.o
	rm -f fuzz/fuzz_demangle_libfuzzer
	rm -f fuzz/diff_cxxfilt fuzz/diff_cxxfilt.o
	rm -rf fuzz/corpus
