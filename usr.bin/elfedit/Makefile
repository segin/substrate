include ../../Makefile.inc

PROG = elfedit
SRCS = elfedit.c
LDADD += ../../usr.lib/elfobj/libelfobj.a
USER_CFLAGS += -iquote ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

$(PROG): ../../usr.lib/elfobj/libelfobj.a

../../usr.lib/elfobj/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

.PHONY: install-man

install: install-man

install-man:
	install -D ../../man/man1/elfedit.1 $(DESTDIR)/usr/share/man/man1/elfedit.1
