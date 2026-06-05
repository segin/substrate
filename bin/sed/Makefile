TOP = ../..
include $(TOP)/Makefile.inc

PROG = sed
SRCS = sed.c sed_util.c sed_parse.c sed_exec.c
OBJS = $(SRCS:.c=.o)

# Target: link the shared regex engine (DT_NEEDED libregex.so.0) and build a
# PIE that loads /sbin/ld.so (DYNAMIC=1).  The host test build keeps the static
# archive.  Without DYNAMIC=1 the link still resolves libregex.so.0 (via the
# libregex.so symlink) but stamps the host interpreter /lib/ld-linux.so.2, so
# the binary won't exec on substrate.
ifdef NATIVE_BUILD
REGEX_LIB = $(TOP)/usr.lib/regex/libregex.a
LDADD = $(REGEX_LIB)
USER_CFLAGS += -DNATIVE_BUILD=1 -D_GNU_SOURCE -DHAVE_MKSTEMP
else
REGEX_LIB = $(TOP)/usr.lib/regex/libregex.so.0
LDADD = -L$(TOP)/usr.lib/regex -l:libregex.so.0
DYNAMIC = 1
endif

USER_CFLAGS += -I$(TOP)/include -I$(TOP)/sys/include -I.

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
