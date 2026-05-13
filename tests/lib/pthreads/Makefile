# Substrate-target torture test for libpthread.
# Builds against the substrate libpthread + libc + libsys.

CROSS  ?= /opt/substrate/bin/i386-unknown-substrate-
CC      = $(CROSS)gcc
CFLAGS  = -Wall -Wextra -O2 -g \
          -I../../../include \
          -I../../../lib/pthreads
LIBS    = -L../../../lib/pthreads -lpthread

TESTS = torture_pthread torture_kernel

all: $(TESTS)

torture_pthread: torture_pthread.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

# torture_kernel pounds the substrate kernel via raw thr_* syscalls;
# include sys/include for SYS_THR_* numbers.
torture_kernel: torture_kernel.c
	$(CC) $(CFLAGS) -I../../../sys/include $< $(LIBS) -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
