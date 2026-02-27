PROG = cp
SRCS = cp.c cp_opts.c cp_path.c cp_hardlink.c cp_atomic.c cp_preserve.c cp_copy.c

include ../../Makefile.inc
include ../../Makefile.bin.inc

ifdef NATIVE_BUILD
USER_CFLAGS += -DCP_HOST_BUILD -D_GNU_SOURCE
endif

CP_COMMON_SRCS = cp_opts.c cp_path.c cp_hardlink.c cp_atomic.c cp_preserve.c cp_copy.c
TEST_CFLAGS = -O1 -g -Wall -Wextra -DCP_HOST_BUILD -D_GNU_SOURCE -I.

UNIT_BINS = test_opts test_path test_hardlink_map test_atomic test_sparse

.PHONY: test unit integration property stress fuzz fuzz-run ci

cp_host: $(SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ $(SRCS)

unit: $(UNIT_BINS)
	@for t in $(UNIT_BINS); do ./$$t; done

test_opts: tests/test_opts.c cp_opts.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_opts.c cp_opts.c

test_path: tests/test_path.c cp_path.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_path.c cp_path.c

test_hardlink_map: tests/test_hardlink_map.c cp_hardlink.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_hardlink_map.c cp_hardlink.c

test_atomic: tests/test_atomic.c cp_atomic.c cp_path.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_atomic.c cp_atomic.c cp_path.c

test_sparse: tests/test_sparse.c cp_copy.c cp_opts.c cp_path.c cp_hardlink.c cp_atomic.c cp_preserve.c
	$(CC) $(TEST_CFLAGS) -DCP_TEST_EXPOSE=1 -o $@ tests/test_sparse.c cp_copy.c cp_opts.c cp_path.c cp_hardlink.c cp_atomic.c cp_preserve.c

integration: cp_host
	sh tests/test_integration.sh ./cp_host

property: cp_host
	sh tests/test_property.sh ./cp_host

stress: cp_host
	sh tests/test_stress.sh ./cp_host

fuzz:
	@if command -v clang >/dev/null 2>&1; then \
		clang -O1 -g -fsanitize=fuzzer,address,undefined -DCP_HOST_BUILD -D_GNU_SOURCE -I. \
			tests/fuzz_opts_path.c cp_opts.c cp_path.c -o fuzz_opts_path; \
		clang -O1 -g -fsanitize=fuzzer,address,undefined -DCP_HOST_BUILD -DCP_TEST_EXPOSE -D_GNU_SOURCE -I. \
			tests/fuzz_sparse_preserve.c cp_opts.c cp_path.c cp_hardlink.c cp_atomic.c cp_preserve.c cp_copy.c -o fuzz_sparse_preserve; \
	else \
		echo "clang not available; skipping fuzz build"; \
	fi

fuzz-run: fuzz
	@if [ -x ./fuzz_opts_path ]; then \
		td=$$(mktemp -d /tmp/cp_fuzz_opts.XXXXXX); \
		cp tests/fuzz/corpus_opts_path/* $$td/; \
		./fuzz_opts_path -max_total_time=5 -max_len=1024 -rss_limit_mb=512 -verbosity=0 $$td; \
		rm -rf $$td; \
	fi
	@if [ -x ./fuzz_sparse_preserve ]; then \
		td=$$(mktemp -d /tmp/cp_fuzz_sparse.XXXXXX); \
		cp tests/fuzz/corpus_sparse_preserve/* $$td/; \
		./fuzz_sparse_preserve -max_total_time=5 -max_len=1024 -rss_limit_mb=512 -verbosity=0 $$td; \
		rm -rf $$td; \
	fi

ci: unit integration property stress fuzz-run

test: ci

clean:
	rm -f $(PROG) $(OBJS) cp_host $(UNIT_BINS) fuzz_opts_path fuzz_sparse_preserve
