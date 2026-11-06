DYNAMIC = 1

include ../../Makefile.inc

PROG = addr2line
SRCS = addr2line.c

USER_CFLAGS += -idirafter ../../include

# The target links the shared objects by their exact file names ($(O):
# i386 in place, x86_64 under obj-x86_64/); a host build of elfobj and
# demangle makes only the archives, so the host build links those.
ifdef NATIVE_BUILD
USER_CFLAGS += -DADDR2LINE_HAVE_ZLIB
ELFOBJ_LIB = $(TOP)/usr.lib/elfobj/libelfobj.a
DEMANGLE_LIB = $(TOP)/usr.lib/demangle/libdemangle.a
LDADD += $(ELFOBJ_LIB) $(DEMANGLE_LIB) -lz
else
ELFOBJ_LIB = $(TOP)/usr.lib/elfobj/$(O)libelfobj.so.0
DEMANGLE_LIB = $(TOP)/usr.lib/demangle/$(O)libdemangle.so.0
LDADD += -L$(TOP)/usr.lib/elfobj -l:libelfobj.so.0 -L$(TOP)/usr.lib/demangle -l:libdemangle.so.0
endif

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

$(O)$(PROG): $(ELFOBJ_LIB) $(DEMANGLE_LIB)

# Recurse unconditionally and let each library decide, as c++filt does: an
# archive is built in two incompatible modes under one name, and each
# library keeps a .build-mode stamp and rebuilds when the mode changes,
# but only if it is entered.  addr2line relinks only when one is touched.
.PHONY: FORCE
FORCE:

$(ELFOBJ_LIB): FORCE
	$(MAKE) -C $(TOP)/usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

$(DEMANGLE_LIB): FORCE
	$(MAKE) -C $(TOP)/usr.lib/demangle $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

# addr2line.1 lives in usr.man/man1 and is installed by usr.man/Makefile.
