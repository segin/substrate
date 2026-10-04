DYNAMIC = 1

include ../../Makefile.inc

PROG = ar
SRCS = ar.c
LDADD += -L$(TOP)/usr.lib/elfobj -l:libelfobj.so.0
USER_CFLAGS += -iquote ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

# $(O): see Makefile.inc (i386 in place, x86_64 under obj-x86_64/) -- both
# the program and the library it waits for live there.
$(O)$(PROG): ../../usr.lib/elfobj/$(O)libelfobj.so.0

../../usr.lib/elfobj/$(O)libelfobj.so.0:
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

install: install-ranlib-link install-man

install-ranlib-link: $(O)$(PROG)
	mkdir -p $(BINDIR)
	ln -sf ar $(BINDIR)/ranlib

install-man:
	mkdir -p $(DESTDIR)/usr/man/man1
	install -m 0644 ../../usr.man/man1/ar.1 $(DESTDIR)/usr/man/man1/ar.1
	install -m 0644 ../../usr.man/man1/ranlib.1 $(DESTDIR)/usr/man/man1/ranlib.1

depend:
	$(CC) $(USER_CFLAGS) -MM $(SRCS) > .depend

-include .depend
