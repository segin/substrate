TOP = ../..
include $(TOP)/Makefile.inc

PROG = sed
SRCS = sed.c sed_util.c sed_parse.c sed_exec.c
OBJS = $(SRCS:.c=.o)

REGEX_LIB = $(TOP)/usr.lib/regex/libregex.a
LDADD = -L$(TOP)/usr.lib/regex -lregex

USER_CFLAGS += -I$(TOP)/include -I$(TOP)/sys/include -I.

ifdef NATIVE_BUILD
USER_CFLAGS += -DNATIVE_BUILD=1 -D_GNU_SOURCE -DHAVE_MKSTEMP
endif

include $(TOP)/Makefile.bin.inc

$(REGEX_LIB):
	$(MAKE) -C $(TOP)/usr.lib/regex

$(PROG): $(REGEX_LIB)

.PHONY: test
test: sed_host
	@echo "Running basic sed tests..."
	@echo "hello world" | ./sed_host 's/world/there/' | grep -q "hello there" && echo "PASS: basic subst" || echo "FAIL: basic subst"
	@printf "a\nb\nc\n" | ./sed_host -n '2p' | grep -q "^b$$" && echo "PASS: -n with p" || echo "FAIL: -n with p"
	@printf "a\nb\nc\n" | ./sed_host '2d' | grep -qv "^b$$" && echo "PASS: delete" || echo "FAIL: delete"
	@printf "a\nb\nc\n" | ./sed_host 'y/abc/ABC/' | grep -q "A" && echo "PASS: y" || echo "FAIL: y"
	@printf "foo\nbar\nbaz\n" | ./sed_host '/bar/d' | grep -qv bar && echo "PASS: regex addr" || echo "FAIL: regex addr"

EXTRA_CLEAN = sed_host

sed_host: $(SRCS) $(REGEX_LIB)
	cc -m32 -O2 -Wall -Wextra -g -DNATIVE_BUILD=1 -D_GNU_SOURCE -DHAVE_MKSTEMP \
	   -I$(TOP)/usr.lib/regex/include -I. \
	   -o $@ $(SRCS) $(REGEX_LIB)
