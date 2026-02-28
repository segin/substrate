PROG = sigtest
SRCS = sigtest.c

include ../../Makefile.inc
include ../../Makefile.bin.inc

clean:
	rm -f $(PROG) *.o

install: all
	mkdir -p ../../dist/bin
	cp $(PROG) ../../dist/bin/
