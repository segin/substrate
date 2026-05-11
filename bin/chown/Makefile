PROG = chown
SRCS = chown.c

USER_CFLAGS += -iquote ../../include

ifdef NATIVE_BUILD
USER_CFLAGS += -DNATIVE_BUILD=1
endif

DYNAMIC = 1

include ../../Makefile.inc
include ../../Makefile.bin.inc

TEST_CFLAGS = -O2 -g -Wall -Wextra -Werror -D_GNU_SOURCE -I. -iquote ../../include
PYTHON ?= python3

.PHONY: integration property fuzz-run ci test

TESTS = ../../tests/bin/chown

chown_host: $(SRCS)
	$(CC) $(TEST_CFLAGS) -o $@ $(SRCS)

integration: chown_host
	sh $(TESTS)/test_integration.sh ./chown_host

property: chown_host
	$(PYTHON) $(TESTS)/test_property.py ./chown_host

fuzz-run: chown_host
	$(PYTHON) $(TESTS)/fuzz_options.py ./chown_host

ci: integration property fuzz-run

test: ci
