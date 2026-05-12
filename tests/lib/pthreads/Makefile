# Substrate-target torture test for libpthread.
# Builds against the substrate libpthread + libc + libsys.

CROSS  ?= /opt/substrate/bin/i386-unknown-substrate-
CC      = $(CROSS)gcc
CFLAGS  = -Wall -Wextra -O2 -g \
          -I../../../include \
          -I../../../lib/pthreads
LIBS    = -L../../../lib/pthreads -lpthread

TESTS = torture_pthread

all: $(TESTS)

torture_pthread: torture_pthread.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
