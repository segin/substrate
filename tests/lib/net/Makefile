# tests/lib/net/Makefile — AF_PACKET smoke test.  Substrate-only;
# the host doesn't share the same AF_PACKET ABI quirks we exercise.

CROSS  ?=
ifeq ($(CROSS),)
  CC      = cc
else
  CC      = $(CROSS)gcc
endif
CFLAGS  = -Wall -Wextra -O2 -g

ifneq ($(CROSS),)
  CFLAGS += -march=i486 -mtune=i486
  CFLAGS += -I../../../include -I../../../sys
endif

TESTS = torture_net torture_inet

all: $(TESTS)

torture_net: torture_net.c
	$(CC) $(CFLAGS) $< -o $@

torture_inet: torture_inet.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
