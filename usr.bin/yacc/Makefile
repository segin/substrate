# Makefile for usr.bin/yacc
#
# Built through Makefile.bin.inc like every other program, so that it gets
# the same three builds from one description: i386 in place, x86_64 under
# obj-x86_64/ (ARCH=x86_64), and the host build (NATIVE_BUILD=1).  The link
# this file used to spell out by hand said -m32 and /sbin/ld.so, and so
# could only ever produce the first.

PROG = yacc
SRCS = main.c reader.c lr0.c lalr.c mkpar.c output.c graph.c symtab.c warshall.c closure.c verbose.c

DYNAMIC = 1
BINDIR = $(DESTDIR)/usr/bin

include ../../Makefile.bin.inc

USER_CFLAGS += -Wno-unused-parameter -I.
ifdef NATIVE_BUILD
USER_CFLAGS += -idirafter ../../include
endif

EXTRA_CLEAN += test_yacc.o y.tab.c y.tab.h y.output

.PHONY: test

test: $(PROG) test_yacc.sh
	sh test_yacc.sh
