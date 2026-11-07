PROG = vi
SRCS = vi.c

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

# view (readonly) and rvi (restricted) are vi under another name: the
# editor reads argv[0].  Copies, not links -- the image is filled from
# dist/ in a way that drops relative symlinks (see bin/grep/Makefile) --
# and the binary is small, libexvi being shared.
ifndef BIN64
install: install-vi-names
install-vi-names: $(O)$(PROG)
	install -D $(O)$(PROG) $(BINDIR)/view
	install -D $(O)$(PROG) $(BINDIR)/rvi
.PHONY: install-vi-names
endif
