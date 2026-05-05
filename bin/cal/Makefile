PROG = cal
SRCS = cal.c cal_opts.c cal_math.c cal_render.c

ifeq ($(NATIVE_BUILD),1)
USER_CFLAGS += -idirafter ../../include
else
USER_CFLAGS += -I../../include
endif

include ../../Makefile.inc
include ../../Makefile.bin.inc

TEST_CFLAGS = -O2 -g -Wall -Wextra -D_GNU_SOURCE -idirafter ../../include
PYTHON ?= python3
UNIT_BINS = test_opts test_math

EXTRA_CLEAN += $(UNIT_BINS)

.PHONY: test unit integration reference property fuzz-run

TESTS = ../../tests/bin/cal

test_opts: $(TESTS)/test_opts.c cal_opts.c cal_math.c cal.h cal_opts.h cal_math.h
	$(CC) $(TEST_CFLAGS) -I. -o $@ $(TESTS)/test_opts.c cal_opts.c cal_math.c

test_math: $(TESTS)/test_math.c cal_math.c cal_opts.c cal.h cal_math.h cal_opts.h
	$(CC) $(TEST_CFLAGS) -I. -o $@ $(TESTS)/test_math.c cal_math.c cal_opts.c

unit: $(UNIT_BINS)
	@for t in $(UNIT_BINS); do ./$$t; done

integration: $(PROG)
	sh $(TESTS)/test_integration.sh ./$(PROG)

reference: $(PROG)
	$(PYTHON) $(TESTS)/test_reference.py ./$(PROG)

property: $(PROG)
	$(PYTHON) $(TESTS)/test_property.py ./$(PROG)

fuzz-run: $(PROG)
	$(PYTHON) $(TESTS)/fuzz_options.py ./$(PROG)

test: unit integration reference property fuzz-run
