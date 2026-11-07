PROG = ex
SRCS = ex.c

ifeq ($(NATIVE_BUILD),1)
# a host build of libexvi makes only the archive; regex is the host's,
# which is what libexvi was compiled against
USER_CFLAGS += -idirafter $(TOP)/include
LDADD = $(TOP)/usr.lib/exvi/libexvi.a
else
LDADD = -L$(TOP)/usr.lib/exvi -l:libexvi.so.0 -L$(TOP)/usr.lib/regex -l:libregex.so.0
endif

DYNAMIC = 1

include ../../Makefile.inc
include ../../Makefile.bin.inc

# rex (restricted ex) is ex under another name: the editor reads argv[0].
# A copy, not a link (see bin/vi/Makefile).
ifndef BIN64
install: install-ex-names
install-ex-names: $(O)$(PROG)
	install -D $(O)$(PROG) $(BINDIR)/rex
.PHONY: install-ex-names
endif
