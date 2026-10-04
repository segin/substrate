# Makefile for usr.bin/slex
#
# Built through Makefile.bin.inc like every other program, so that it gets
# the same three builds from one description: i386 in place, x86_64 under
# obj-x86_64/ (ARCH=x86_64), and the host build (NATIVE_BUILD=1).  The link
# this file used to spell out by hand said -m32 and /sbin/ld.so, and so
# could only ever produce the first.

PROG = slex
SRCS = main.c options.c parser.c symtab.c rules.c regex.c dfa.c codegen.c \
       util.c

DYNAMIC = 1
BINDIR = $(DESTDIR)/usr/bin

include ../../Makefile.bin.inc

USER_CFLAGS += -I.
ifdef NATIVE_BUILD
USER_CFLAGS += -idirafter ../../include
endif

EXTRA_CLEAN += skel.h skel.h.tmp

# The fixed text of the generated scanner, as C string arrays.  It is
# generated beside the sources and is the same for every build, so every
# mode's codegen object waits for the one file.
skel.h: scanner.skel mkskel.awk
	awk -f mkskel.awk scanner.skel > $@.tmp
	mv $@.tmp $@

$(O)codegen.o codegen.host.o: skel.h

.PHONY: test

# Behaviour tests: build for the host and run tests/usr.bin/slex.
test:
	$(MAKE) clean
	$(MAKE) NATIVE_BUILD=1
	sh ../../tests/usr.bin/slex/run_tests.sh ./$(PROG)
