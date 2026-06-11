# tests/lib/futex/Makefile
#
# Portable POSIX futex(2) torture suite.  Runs on Linux (baseline oracle)
# with the host toolchain; cross-builds against substrate's libc +
# libpthread when CROSS is set.
#
# Host build (default):  make ; ./torture_futex
# Substrate cross build: make CROSS=/opt/substrate/bin/i386-unknown-substrate-

CROSS  ?=
ifeq ($(CROSS),)
  CC      = cc
else
  CC      = $(CROSS)gcc
endif
CFLAGS  = -Wall -Wextra -O2 -g

ifneq ($(CROSS),)
  CFLAGS  += -march=i486 -mtune=i486
  LDFLAGS += -Wl,--copy-dt-needed-entries -Wl,-rpath-link,/opt/substrate/i386-unknown-substrate/lib
  PTHREAD  = -L../../../lib/pthread -l:libpthread.so.0
else
  PTHREAD  = -pthread
endif

TESTS = torture_futex
all: $(TESTS)

torture_futex: torture_futex.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(PTHREAD)

clean:
	rm -f $(TESTS)

.PHONY: all clean
