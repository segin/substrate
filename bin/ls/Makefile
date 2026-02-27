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

.PHONY: test

test_ls: $(SRCS) tests/test_ls.c
	cc $(TEST_CFLAGS) -o $@ tests/test_ls.c ls_opts.c ls_print.c ls_sort.c ls_traverse.c ls_colors.c

test: test_ls
	./test_ls
