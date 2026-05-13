# torture_kernel.c is portable POSIX — builds anywhere with a C
# compiler and pthreads.  torture_pthread.c is substrate-target
# (links against the substrate libpthread directly).
#
# Default build is HOST: produces torture_kernel from the host
# toolchain so you can run it on Linux/FreeBSD/macOS as the
# comparison baseline.
#
# For the substrate cross-build:
#     make CROSS=/opt/substrate/bin/i386-unknown-substrate-
#
# That produces both binaries linked against substrate's libpthread
# and runs them on the substrate target.

CROSS  ?=
CC      = $(CROSS)cc
CFLAGS  = -Wall -Wextra -O2 -g

# Host build (no CROSS): -pthread links against the system pthreads.
# Cross build (CROSS set): point at substrate's libpthread + headers.
ifeq ($(CROSS),)
  PTHREAD = -pthread
  KERNEL_INC =
  PTHREAD_INC =
else
  PTHREAD = -L../../../lib/pthreads -lpthread
  PTHREAD_INC = -I../../../include -I../../../lib/pthreads
endif

TESTS_PORTABLE = torture_kernel
TESTS_SUBSTRATE = torture_pthread

# Default: just the portable suite.  Substrate-specific tests are
# only built when CROSS is set.
ifeq ($(CROSS),)
  TESTS = $(TESTS_PORTABLE)
else
  TESTS = $(TESTS_PORTABLE) $(TESTS_SUBSTRATE)
endif

all: $(TESTS)

# Portable: POSIX pthreads, runs on Linux/FreeBSD/macOS/substrate.
torture_kernel: torture_kernel.c
	$(CC) $(CFLAGS) $(PTHREAD_INC) $< $(PTHREAD) -o $@

# Substrate-target only.
torture_pthread: torture_pthread.c
	$(CC) $(CFLAGS) $(PTHREAD_INC) $< $(PTHREAD) -o $@

clean:
	rm -f $(TESTS_PORTABLE) $(TESTS_SUBSTRATE)

.PHONY: all clean
