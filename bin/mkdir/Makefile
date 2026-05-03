PROG = mkdir
SRCS = mkdir.c mkdir_opts.c mkdir_parents.c

USER_CFLAGS += -iquote ../../include

ifdef NATIVE_BUILD
USER_CFLAGS += -D_GNU_SOURCE
endif

include ../../Makefile.inc
include ../../Makefile.bin.inc

OBJS += modeparse.o
EXTRA_CLEAN += modeparse.o mkdir_host $(UNIT_BINS)

$(PROG): modeparse.o

modeparse.o: ../../lib/modeparse.c ../../include/modeparse.h $(BUILD_RULE_DEPS)
	$(CC) $(USER_CFLAGS) -c ../../lib/modeparse.c -o $@

TEST_CFLAGS = -O2 -g -Wall -Wextra -Werror -D_GNU_SOURCE -I. -iquote ../../include
PYTHON ?= python3
UNIT_BINS = test_opts test_mode test_parents

.PHONY: test unit integration property fuzz-run ci

mkdir_host: $(SRCS) ../../lib/modeparse.c ../../include/modeparse.h
	$(CC) $(TEST_CFLAGS) -o $@ $(SRCS) ../../lib/modeparse.c

test_opts: tests/test_opts.c mkdir_opts.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_opts.c mkdir_opts.c

test_mode: tests/test_mode.c ../../lib/modeparse.c ../../include/modeparse.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_mode.c ../../lib/modeparse.c

test_parents: tests/test_parents.c mkdir_parents.c mkdir_opts.c
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_parents.c mkdir_parents.c mkdir_opts.c

unit: $(UNIT_BINS)
	@for t in $(UNIT_BINS); do ./$$t; done

integration: mkdir_host
	sh tests/test_integration.sh ./mkdir_host

property: mkdir_host
	$(PYTHON) tests/test_property.py ./mkdir_host

fuzz-run: mkdir_host
	$(PYTHON) tests/fuzz_options.py ./mkdir_host
	$(PYTHON) tests/fuzz_paths.py ./mkdir_host

ci: unit integration property fuzz-run

test: ci
