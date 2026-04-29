PROG = rm
SRCS = rm.c rm_opts.c rm_safety.c rm_walk.c

include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
USER_CFLAGS += -idirafter ../../include
else
USER_CFLAGS += -I../../include
endif

include ../../Makefile.bin.inc

TEST_CFLAGS = -O1 -g -Wall -Wextra -D_GNU_SOURCE -idirafter ../../include
UNIT_BINS = test_opts test_safety test_walk
PYTHON ?= python3

EXTRA_CLEAN += rm_host $(UNIT_BINS)

.PHONY: test unit integration property fuzz-run

rm_host: $(SRCS) rm.h rm_opts.h rm_safety.h rm_walk.h
	$(CC) $(TEST_CFLAGS) -o $@ $(SRCS)

TESTS = ../../tests/bin/rm

test_opts: $(TESTS)/test_opts.c rm_opts.c rm.h rm_opts.h
	$(CC) $(TEST_CFLAGS) -I. -o $@ $(TESTS)/test_opts.c rm_opts.c

test_safety: $(TESTS)/test_safety.c rm_safety.c rm.h rm_safety.h
	$(CC) $(TEST_CFLAGS) -I. -o $@ $(TESTS)/test_safety.c rm_safety.c

test_walk: $(TESTS)/test_walk.c rm_opts.c rm_safety.c rm_walk.c rm.h rm_opts.h rm_safety.h rm_walk.h
	$(CC) $(TEST_CFLAGS) -I. -o $@ $(TESTS)/test_walk.c rm_opts.c rm_safety.c rm_walk.c

unit: $(UNIT_BINS)
	@for t in $(UNIT_BINS); do ./$$t; done

integration: rm_host
	sh $(TESTS)/test_integration.sh ./rm_host

property: rm_host
	$(PYTHON) $(TESTS)/test_property.py ./rm_host

fuzz-run: rm_host
	$(PYTHON) $(TESTS)/fuzz_options.py ./rm_host

test: unit integration property fuzz-run
