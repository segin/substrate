# tests/lib/pty/Makefile
#
# Portable POSIX test program for Unix98 PTYs.  Same CROSS=PREFIX
# convention as tests/lib/{sockets,ipc}.

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
  # No pthread dependency, but match the include path layout the
  # other testsuites use so future extensions can pick up libpthread
  # without restructuring.
  CFLAGS += -I../../../include -I../../../sys
endif

TESTS = torture_pty torture_ctty

all: $(TESTS)

torture_pty: torture_pty.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

torture_ctty: torture_ctty.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
