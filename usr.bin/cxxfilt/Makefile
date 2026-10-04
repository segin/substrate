# Makefile for usr.bin/cxxfilt (c++filt)
#
# Built through Makefile.bin.inc like every other program, so that it gets
# the same three builds from one description: i386 in place, x86_64 under
# obj-x86_64/ (ARCH=x86_64), and the host build (NATIVE_BUILD=1).  The link
# this file used to spell out by hand said -m32 and /sbin/ld.so, and so
# could only ever produce the first.

PROG = c++filt
SRCS = cxxfilt.c

DYNAMIC = 1
BINDIR = $(DESTDIR)/usr/bin

include ../../Makefile.bin.inc

# The demangler lives in usr.lib/demangle.  The target links the shared
# object by its exact file name ($(O): i386 in place, x86_64 under
# obj-x86_64/; Makefile.bin.inc points the -L there); the host build has
# only the archive.
ifdef NATIVE_BUILD
USER_CFLAGS += -idirafter ../../include
DEMANGLE_LIB = $(TOP)/usr.lib/demangle/libdemangle.a
LDADD = $(DEMANGLE_LIB)
else
DEMANGLE_LIB = $(TOP)/usr.lib/demangle/$(O)libdemangle.so.0
LDADD = -L$(TOP)/usr.lib/demangle -l:libdemangle.so.0
endif

$(O)$(PROG): $(DEMANGLE_LIB)

# Recurse unconditionally and let usr.lib/demangle decide, as bin/join does
# for its library: the archive is built in two incompatible modes under
# one name, and a rule that fires only when it is missing would hand a
# target build's i386 libdemangle.a to the host linker.  usr.lib/demangle
# keeps a .build-mode stamp and rebuilds when the mode changes, but only
# if it is entered.  c++filt relinks only when the library is touched.
.PHONY: FORCE
FORCE:

$(DEMANGLE_LIB): FORCE
	$(MAKE) -C $(TOP)/usr.lib/demangle $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

.PHONY: install-man

install: install-man

install-man:
	mkdir -p $(DESTDIR)/usr/man/man1
	install -m 0644 c++filt.1 $(DESTDIR)/usr/man/man1/c++filt.1
