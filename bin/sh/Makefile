PROG=sh
SRCS=sh.c lexer.c parser.c expand.c exec.c shell_var.c util.c job.c
OBJS=$(SRCS:.c=.o)

include ../../Makefile.bin.inc

ifndef NATIVE_BUILD
USER_CFLAGS += -I$(TOP)/sys
endif

test_lexer: lexer.c tests/test_lexer.c
	gcc -g -I. -o $@ lexer.c tests/test_lexer.c

test_parser: lexer.c parser.c tests/test_parser.c
	gcc -g -I. -o $@ lexer.c parser.c tests/test_parser.c

test_expand: expand.c shell_var.c util.c tests/test_expand.c
	gcc -g -I. -o $@ expand.c shell_var.c util.c tests/test_expand.c

fuzz_parser: lexer.c parser.c shell_var.c util.c tests/fuzz_parser.c
	$(CC) -g -I. -o $@ $^

test: test_lexer test_parser test_expand
	./test_lexer
	./test_parser
	./test_expand
