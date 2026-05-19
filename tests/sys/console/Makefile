# tests/sys/console/Makefile — host-side console state-machine tests.

CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2

PROG   = test_vt_wrap
SRCS   = test_vt_wrap.c

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

run: $(PROG)
	./$(PROG)

clean:
	rm -f $(PROG)

.PHONY: all run clean
