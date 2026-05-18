# tests/lib/signal/Makefile — signal-interruptibility test.  Same
# host/cross convention as the other testsuites.

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
  CFLAGS += -I../../../include -I../../../sys
endif

TESTS = torture_signal

all: $(TESTS)

torture_signal: torture_signal.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
