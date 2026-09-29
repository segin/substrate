PROG = seq
SRCS = seq.c

DYNAMIC = 1

BINDIR = $(DESTDIR)/usr/bin

include ../../Makefile.bin.inc

.PHONY: test

test:
	$(MAKE) clean
	$(MAKE) NATIVE_BUILD=1
	sh ../../tests/usr.bin/seq/run_tests.sh ./$(PROG)
