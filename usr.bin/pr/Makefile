PROG = pr
SRCS = pr.c

include ../../Makefile.bin.inc

.PHONY: test

test:
	$(MAKE) clean
	$(MAKE) NATIVE_BUILD=1
	$(MAKE) -C tests test PR=../pr
