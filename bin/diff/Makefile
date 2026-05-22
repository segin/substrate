PROG = diff
SRCS = diff.c
LDADD = -L$(TOP)/usr.lib/regex -l:libregex.so.0
REGEX_LIB = $(TOP)/usr.lib/regex/libregex.a
.DEFAULT_GOAL := all

$(REGEX_LIB):
	$(MAKE) -C $(TOP)/usr.lib/regex clean all

$(PROG): $(REGEX_LIB)

DYNAMIC = 1

include ../../Makefile.inc
include ../../Makefile.bin.inc
