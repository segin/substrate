DYNAMIC = 1

include ../../Makefile.inc

PROG = ar
SRCS = ar.c
# A host build has no libelfobj.so.0 (usr.lib/elfobj builds only the
# archive with NATIVE_BUILD=1), so it links the archive, as usr.bin/ld does.
ifdef NATIVE_BUILD
ELFOBJ_LIB = ../../usr.lib/elfobj/$(O)libelfobj.a
LDADD += $(ELFOBJ_LIB)
else
ELFOBJ_LIB = ../../usr.lib/elfobj/$(O)libelfobj.so.0
LDADD += -L$(TOP)/usr.lib/elfobj -l:libelfobj.so.0
endif
USER_CFLAGS += -iquote ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

# $(O): see Makefile.inc (i386 in place, x86_64 under obj-x86_64/) -- both
# the program and the library it waits for live there.
$(O)$(PROG): $(ELFOBJ_LIB)

$(ELFOBJ_LIB):
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

# ar.1 and ranlib.1 live in usr.man/man1 and are installed by usr.man/Makefile.
install: install-ranlib-link

install-ranlib-link: $(O)$(PROG)
	mkdir -p $(BINDIR)
	ln -sf ar $(BINDIR)/ranlib

depend:
	$(CC) $(USER_CFLAGS) -MM $(SRCS) > .depend

-include .depend
