PROG = cpio
SRCS = cpio.c

DYNAMIC = 1

include ../../Makefile.bin.inc

BINDIR = $(DESTDIR)/usr/bin
USER_CFLAGS += -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700

.PHONY: test

# the tests are tests/usr.bin/cpio's, run on a host build
test:
	$(MAKE) clean
	$(MAKE) -C ../../tests/usr.bin/cpio run
