PROG=sh
SRCS=sh.c lexer.c parser.c expand.c exec.c shell_var.c util.c job.c prompt.c
OBJS=$(SRCS:.c=.o)

include ../../Makefile.bin.inc

ifndef NATIVE_BUILD
USER_CFLAGS += -I$(TOP)/sys
endif

test_lexer: lexer.c util.c shell_var.c tests/test_lexer.c
	gcc -g -I. -o $@ lexer.c util.c shell_var.c tests/test_lexer.c

test_parser: lexer.c parser.c util.c shell_var.c tests/test_parser.c
	gcc -g -I. -o $@ lexer.c parser.c util.c shell_var.c tests/test_parser.c

test_expand: expand.c shell_var.c util.c tests/test_expand.c
	gcc -g -I. -o $@ expand.c shell_var.c util.c tests/test_expand.c

test_shell_var: shell_var.c util.c tests/test_shell_var.c
	gcc -g -I. -o $@ shell_var.c util.c tests/test_shell_var.c

test_job: job.c tests/test_job.c
	gcc -g -I. -o $@ job.c tests/test_job.c

test_util: util.c tests/test_util.c
	gcc -g -I. -o $@ util.c tests/test_util.c

fuzzer: lexer.c parser.c util.c shell_var.c tests/fuzz_parser.c
	clang -g -fsanitize=fuzzer,address -I. -o $@ lexer.c parser.c util.c shell_var.c tests/fuzz_parser.c

fuzz_ps1: prompt.c util.c shell_var.c tests/fuzz_ps1_parser.c
	clang -g -fsanitize=fuzzer,address -I. -o $@ prompt.c util.c shell_var.c tests/fuzz_ps1_parser.c

fuzz_parser: lexer.c parser.c shell_var.c util.c tests/fuzz_parser.c
	$(CC) -g -I. -o $@ $^

test: all test_lexer test_parser test_expand test_shell_var test_job test_util
	./test_lexer
	./test_parser
	./test_expand
	./test_shell_var
	./test_job
	./test_util
	./test_prompt
	sh tests/test_prompt_mode.sh

test_prompt: prompt.c util.c shell_var.c tests/test_prompt.c
	gcc -g -I. -o $@ prompt.c util.c shell_var.c tests/test_prompt.c
