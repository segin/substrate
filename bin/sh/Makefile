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

# sh64: this shell as a native 64-bit program, linked statically against
# the 64-bit libraries (make -C lib ARCH=x86_64) -- there is no 64-bit
# dynamic linker yet.  It runs on the x86_64 kernel only, and is installed
# as /bin/sh64 beside the 32-bit userland.
SH64_LIBS = $(TOP)/lib/edit/obj-x86_64/libedit.a $(TOP)/lib/c/obj-x86_64/libc.a \
            $(TOP)/lib/sys/obj-x86_64/libsys.a $(TOP)/lib/m/obj-x86_64/libm.a
SH64_CFLAGS = -m64 -march=x86-64 $(WARNFLAGS) -g -fno-stack-protector \
              -std=c2x -nostdinc -fno-pie -I$(TOP)/include -I$(TOP)/sys/include \
              -I$(TOP)/sys -I$(shell $(CC) -m64 -print-file-name=include)

sh64: $(SRCS) $(SH64_LIBS) $(TOP)/lib/c/obj-x86_64/crt0.o
	$(CC) $(SH64_CFLAGS) -o $@ $(SRCS) -nostdlib -static -no-pie \
	      -Wl,-z,max-page-size=0x1000 $(TOP)/lib/c/obj-x86_64/crt0.o \
	      -Wl,--start-group $(SH64_LIBS) \
	      $(shell $(CC) -m64 -print-libgcc-file-name) -Wl,--end-group
	$(call BRAND_SUBSTRATE,$@)

$(SH64_LIBS) $(TOP)/lib/c/obj-x86_64/crt0.o:
	$(MAKE) -C $(TOP)/lib ARCH=x86_64

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
