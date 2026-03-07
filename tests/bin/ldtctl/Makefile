CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -DHOST_TEST
INCLUDES = -I../../../sys -I../../../sys/include

.PHONY: all clean test

all: test

test_ldtctl: test_ldtctl.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $<

test: test_ldtctl
	./test_ldtctl

clean:
	rm -f test_ldtctl
