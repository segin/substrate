# tests/lib/input/Makefile
#
# Cross-mode build, same CROSS=PREFIX convention as tests/lib/{pty,sockets}.

CROSS  ?=
ifeq ($(CROSS),)
  CC = cc
else
  CC = $(CROSS)gcc
endif
CFLAGS = -Wall -Wextra -O2 -g

ifneq ($(CROSS),)
  CFLAGS  += -march=i486 -mtune=i486
  CFLAGS  += -I../../../include -I../../../sys/include
  LDFLAGS += -Wl,--copy-dt-needed-entries \
             -Wl,-rpath-link,/opt/substrate/i386-unknown-substrate/lib
endif

TESTS = test_mouse

all: $(TESTS)

test_mouse: test_mouse.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
