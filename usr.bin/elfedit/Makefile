DYNAMIC = 1

include ../../Makefile.inc

PROG = elfedit
SRCS = elfedit.c
LDADD += -L$(TOP)/usr.lib/elfobj -l:libelfobj.so.0
USER_CFLAGS += -iquote ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

ELFOBJ_SHLIB = ../../usr.lib/elfobj/libelfobj.so.0
$(PROG): $(ELFOBJ_SHLIB)

$(ELFOBJ_SHLIB):
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

.PHONY: install-man

install: install-man

install-man:
	install -D ../../usr.man/man1/elfedit.1 $(DESTDIR)/usr/man/man1/elfedit.1
