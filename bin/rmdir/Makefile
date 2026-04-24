PROG = rmdir
SRCS = rmdir.c rmdir_opts.c rmdir_parents.c

include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
USER_CFLAGS += -idirafter ../../include
else
USER_CFLAGS += -I../../include
endif

include ../../Makefile.bin.inc

TEST_CFLAGS = -O1 -g -Wall -Wextra -D_GNU_SOURCE -idirafter ../../include
UNIT_BINS = test_opts test_parents
PYTHON ?= python3

EXTRA_CLEAN += rmdir_host $(UNIT_BINS)

.PHONY: test unit integration property race fuzz-run

rmdir_host: $(SRCS) rmdir.h rmdir_opts.h rmdir_parents.h
	$(CC) $(TEST_CFLAGS) -o $@ $(SRCS)

test_opts: tests/test_opts.c rmdir_opts.c rmdir.h rmdir_opts.h
	$(CC) $(TEST_CFLAGS) -I. -o $@ tests/test_opts.c rmdir_opts.c

test_parents: tests/test_parents.c rmdir_parents.c rmdir.h rmdir_opts.h rmdir_parents.h
	$(CC) $(TEST_CFLAGS) -I. -o $@ tests/test_parents.c rmdir_parents.c

unit: $(UNIT_BINS)
	@for t in $(UNIT_BINS); do ./$$t; done

integration: rmdir_host
	sh tests/test_integration.sh ./rmdir_host

property: rmdir_host
	$(PYTHON) tests/test_property.py ./rmdir_host

race: rmdir_host
	$(PYTHON) tests/test_race.py ./rmdir_host

fuzz-run: rmdir_host
	$(PYTHON) tests/fuzz_options.py ./rmdir_host
	$(PYTHON) tests/fuzz_paths.py ./rmdir_host

test: unit integration property race fuzz-run
