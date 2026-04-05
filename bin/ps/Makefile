TOP = ../..

TEST_CFLAGS = -O2 -g -Wall -Wextra -Werror -DNATIVE_BUILD=1 -I. -I$(TOP)/include
EXTRA_CLEAN += test_ps

.PHONY: test

test_ps: ps_parse.c ps_format.c tests/test_ps.c
	cc $(TEST_CFLAGS) -o $@ tests/test_ps.c ps_parse.c ps_format.c

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
LDADD = -L$(TOP)/lib/sys -lsys

include $(TOP)/Makefile.inc
include $(TOP)/Makefile.bin.inc

BRAND_CMD =
endif
