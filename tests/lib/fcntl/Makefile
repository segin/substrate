# tests/lib/fcntl/Makefile
#
# Pure POSIX fcntl/dup/dup2/dup3 stress.  Same conventions as
# tests/lib/ipc/Makefile.
#
# Host build (default):       make ; ./torture_fcntl
# Substrate cross build:      make CROSS=/opt/substrate/bin/i386-unknown-substrate-

CROSS  ?=
ifeq ($(CROSS),)
  CC      = cc
else
  CC      = $(CROSS)gcc
endif
CFLAGS  = -Wall -Wextra -O2 -g

ifneq ($(CROSS),)
  CFLAGS += -march=i486 -mtune=i486
  LDFLAGS += -Wl,--copy-dt-needed-entries -Wl,-rpath-link,/opt/substrate/i386-unknown-substrate/lib
endif

TESTS = torture_fcntl

all: $(TESTS)

torture_fcntl: torture_fcntl.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
