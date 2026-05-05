PROG = grep
SRCS = grep.c
LDADD = -L$(TOP)/usr.lib/regex -lregex
REGEX_LIB = $(TOP)/usr.lib/regex/libregex.a
.DEFAULT_GOAL := all
USER_CFLAGS += -I$(TOP)/include -I$(TOP)/sys/include

$(REGEX_LIB):
	$(MAKE) -C $(TOP)/usr.lib/regex clean all

$(PROG): $(REGEX_LIB)

include ../../Makefile.inc
include ../../Makefile.bin.inc
