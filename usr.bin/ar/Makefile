include ../../Makefile.inc

PROG = ar
SRCS = ar.c
LDADD += ../../usr.lib/elfobj/libelfobj.a
USER_CFLAGS += -iquote ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

$(PROG): ../../usr.lib/elfobj/libelfobj.a

../../usr.lib/elfobj/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

install: install-ranlib-link install-man

install-ranlib-link: $(PROG)
	mkdir -p $(BINDIR)
	ln -sf ar $(BINDIR)/ranlib

install-man:
	mkdir -p $(DESTDIR)/usr/share/man/man1
	install -m 0644 ../../man/man1/ar.1 $(DESTDIR)/usr/share/man/man1/ar.1
	install -m 0644 ../../man/man1/ranlib.1 $(DESTDIR)/usr/share/man/man1/ranlib.1

depend:
	$(CC) $(USER_CFLAGS) -MM $(SRCS) > .depend

-include .depend
