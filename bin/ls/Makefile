TOP = ../..
include $(TOP)/Makefile.inc

PROG = ls
SRCS = ls.c ls_opts.c ls_print.c ls_sort.c ls_traverse.c ls_colors.c
OBJS = $(SRCS:.c=.o)

include $(TOP)/Makefile.bin.inc

USER_CFLAGS += -I$(TOP)/sys

ifdef NATIVE_BUILD
USER_CFLAGS += -DNATIVE_BUILD=1
endif

TEST_CFLAGS = -O2 -g -Wall -Wextra -Werror -DTEST -DNATIVE_BUILD=1 -D_GNU_SOURCE -I.
HOST_CFLAGS = -O2 -g -Wall -Wextra -Werror -DNATIVE_BUILD=1 -D_GNU_SOURCE -I.
EXTRA_CLEAN += test_ls ls_host

.PHONY: test unit integration property stress ci

test_ls: $(SRCS) tests/test_ls.c
	cc $(TEST_CFLAGS) -o $@ tests/test_ls.c ls_opts.c ls_print.c ls_sort.c ls_traverse.c ls_colors.c

ls_host: $(SRCS)
	cc $(HOST_CFLAGS) -o $@ $(SRCS)

unit: test_ls
	./test_ls

integration: ls_host
	sh tests/test_integration.sh ./ls_host

property: ls_host
	sh tests/test_property.sh ./ls_host

stress: ls_host
	LS_STRESS_COUNT=$${LS_STRESS_COUNT:-10000} sh tests/test_stress.sh ./ls_host

ci: unit integration property stress

test: ci
