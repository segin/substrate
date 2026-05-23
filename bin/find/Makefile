PROG = find
SRCS = find_main.c find_parse.c find_eval.c find_traverse.c
LDADD = -L$(TOP)/usr.lib/regex -l:libregex.so.0
REGEX_LIB = $(TOP)/usr.lib/regex/libregex.a

$(REGEX_LIB):
	$(MAKE) -C $(TOP)/usr.lib/regex

$(PROG): $(REGEX_LIB)

DYNAMIC = 1

include ../../Makefile.inc
include ../../Makefile.bin.inc
