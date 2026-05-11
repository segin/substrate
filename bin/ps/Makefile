TOP = ../..

# Default target must come before any other rule, otherwise `make -C
# bin/ps` from the recursive top-level build picks `test_ps` (the
# first rule) instead of the program.
.DEFAULT_GOAL := all

TEST_CFLAGS = -O2 -g -Wall -Wextra -Werror -DNATIVE_BUILD=1 -I. -I$(TOP)/include
EXTRA_CLEAN += test_ps

.PHONY: test

TESTS = $(TOP)/tests/bin/ps

test_ps: ps_parse.c ps_format.c $(TESTS)/test_ps.c
	cc $(TEST_CFLAGS) -o $@ $(TESTS)/test_ps.c ps_parse.c ps_format.c

test: test_ps
	./test_ps

ifdef NATIVE_BUILD
all:
	@echo "Skipping ps for host build"
clean:
	rm -f test_ps
install:
else
PROG = ps
SRCS = ps.c ps_parse.c ps_format.c
DYNAMIC = 1
# libsys.so.0 is already pulled in transitively via libc.so.0's
# DT_NEEDED, but keep the explicit -l so the static link mode (if
# anyone toggles DYNAMIC=0 here) still resolves it.
LDADD = -L$(TOP)/lib/sys -l:libsys.so.0

include $(TOP)/Makefile.inc
include $(TOP)/Makefile.bin.inc

BRAND_CMD =
endif
