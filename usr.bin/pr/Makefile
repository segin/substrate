PROG = pr
SRCS = pr.c

DYNAMIC = 1

include ../../Makefile.bin.inc

.PHONY: test

test:
	$(MAKE) clean
	$(MAKE) NATIVE_BUILD=1
	$(MAKE) -C tests test PR=../pr
