PROG=sh
SRCS=sh.c lexer.c parser.c expand.c exec.c shell_var.c util.c job.c prompt.c
OBJS=$(SRCS:.c=.o)
LIBEDIT_A=$(TOP)/lib/edit/libedit.a
DYNAMIC = 1

include ../../Makefile.bin.inc

# Use the libedit.so.0 shared library so ld.so resolves it at
# runtime alongside libc.so.0.  Requires lib/edit to have been
# built (sbin/Makefile pulls it in via the lib/ chain).
LDADD = -L$(TOP)/lib/edit -l:libedit.so.0

libedit_target:
	$(MAKE) -C $(TOP)/lib/edit clean all

libedit_host:
	$(MAKE) -C $(TOP)/lib/edit NATIVE_BUILD=1 clean all

$(PROG): libedit_target

ifndef NATIVE_BUILD
USER_CFLAGS += -I$(TOP)/sys
endif

TESTS = ../../tests/bin/sh

test_lexer: lexer.c util.c shell_var.c $(TESTS)/test_lexer.c
	$(HOSTCC) -g -I. -o $@ lexer.c util.c shell_var.c $(TESTS)/test_lexer.c

test_parser: lexer.c parser.c util.c shell_var.c $(TESTS)/test_parser.c
	$(HOSTCC) -g -I. -o $@ lexer.c parser.c util.c shell_var.c $(TESTS)/test_parser.c

test_expand: expand.c shell_var.c util.c $(TESTS)/test_expand.c
	$(HOSTCC) -g -I. -o $@ expand.c shell_var.c util.c $(TESTS)/test_expand.c

test_shell_var: shell_var.c util.c $(TESTS)/test_shell_var.c
	$(HOSTCC) -g -I. -o $@ shell_var.c util.c $(TESTS)/test_shell_var.c

test_job: job.c $(TESTS)/test_job.c
	$(HOSTCC) -g -I. -o $@ job.c $(TESTS)/test_job.c

test_util: util.c $(TESTS)/test_util.c
	$(HOSTCC) -g -I. -o $@ util.c $(TESTS)/test_util.c

test_exec: exec.c lexer.c parser.c expand.c shell_var.c job.c util.c prompt.c $(TESTS)/test_exec.c
	$(HOSTCC) -g -I. -o $@ exec.c lexer.c parser.c expand.c shell_var.c job.c util.c prompt.c $(TESTS)/test_exec.c

test_exec_property: exec.c lexer.c parser.c expand.c shell_var.c job.c util.c prompt.c $(TESTS)/test_exec_property.c
	$(HOSTCC) -g -I. -o $@ exec.c lexer.c parser.c expand.c shell_var.c job.c util.c prompt.c $(TESTS)/test_exec_property.c

fuzzer: lexer.c parser.c util.c shell_var.c $(TESTS)/fuzz_parser.c
	clang -g -fsanitize=fuzzer,address -I. -o $@ lexer.c parser.c util.c shell_var.c $(TESTS)/fuzz_parser.c

fuzz_ps1: prompt.c util.c shell_var.c $(TESTS)/fuzz_ps1_parser.c
	clang -g -fsanitize=fuzzer,address -I. -o $@ prompt.c util.c shell_var.c $(TESTS)/fuzz_ps1_parser.c

fuzz_parser: lexer.c parser.c shell_var.c util.c $(TESTS)/fuzz_parser.c
	clang -g -fsanitize=fuzzer,address -I. -o $@ $^

fuzz_expand: expand.c lexer.c parser.c shell_var.c util.c $(TESTS)/fuzz_expand.c
	clang -g -fsanitize=fuzzer,address -I. -o $@ expand.c lexer.c parser.c shell_var.c util.c $(TESTS)/fuzz_expand.c

fuzz_expand_status: expand.c lexer.c parser.c shell_var.c util.c $(TESTS)/fuzz_expand_status.c
	clang -g -fsanitize=fuzzer,address -I. -o $@ expand.c lexer.c parser.c shell_var.c util.c $(TESTS)/fuzz_expand_status.c

sh_host: $(SRCS) libedit_host
	$(CC) -g -D_GNU_SOURCE -I. -include $(TOP)/include/histedit.h -o $@ $(SRCS) -L$(TOP)/lib/edit -l:libedit.a

test: all test_lexer test_parser test_expand test_shell_var test_job test_util test_exec test_exec_property test_prompt sh_host
	./test_lexer
	./test_parser
	./test_expand
	./test_shell_var
	./test_job
	./test_util
	./test_exec
	./test_exec_property
	./test_prompt
	SH="./sh_host" sh $(TESTS)/test_prompt_mode.sh
	SH="./sh_host" sh $(TESTS)/test_regressions.sh

test_prompt: prompt.c util.c shell_var.c $(TESTS)/test_prompt.c
	$(HOSTCC) -g -I. -o $@ prompt.c util.c shell_var.c $(TESTS)/test_prompt.c
