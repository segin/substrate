PROG = pr
SRCS = pr.c

DYNAMIC = 1

include ../../Makefile.bin.inc

.PHONY: test

# the tests are tests/usr.bin/pr's, run on a host build
test:
	$(MAKE) clean
	$(MAKE) -C ../../tests/usr.bin/pr run
