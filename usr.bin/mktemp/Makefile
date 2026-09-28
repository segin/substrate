PROG = mktemp
SRCS = mktemp.c

DYNAMIC = 1

BINDIR = $(DESTDIR)/usr/bin

include ../../Makefile.bin.inc

.PHONY: test

test:
	$(MAKE) clean
	$(MAKE) NATIVE_BUILD=1
	sh ../../tests/usr.bin/mktemp/run_tests.sh ./$(PROG)
