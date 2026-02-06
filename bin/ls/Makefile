TOP = ../..
include $(TOP)/Makefile.inc

PROG = ls
SRCS = ls.c ls_opts.c ls_print.c ls_sort.c ls_traverse.c
OBJS = $(SRCS:.c=.o)

include $(TOP)/Makefile.bin.inc

USER_CFLAGS += -I$(TOP)/sys

test_ls: $(SRCS) tests/test_ls.c
	gcc -m32 -g -o $@ tests/test_ls.c ls_opts.c ls_print.c ls_sort.c ls_traverse.c -I. -DTEST

test: test_ls
	./test_ls
