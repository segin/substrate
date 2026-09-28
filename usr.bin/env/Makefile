PROG = env
SRCS = env.c

DYNAMIC = 1

# Scripts name it as #!/usr/bin/env.
BINDIR = $(DESTDIR)/usr/bin

include ../../Makefile.bin.inc

.PHONY: test

test:
	$(MAKE) clean
	$(MAKE) NATIVE_BUILD=1
	sh ../../tests/usr.bin/env/run_tests.sh ./$(PROG)
