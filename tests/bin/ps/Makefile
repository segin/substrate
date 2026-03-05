CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-pointer-to-int-cast -DHOST_TEST
INCLUDES = -I../../../sys -I../../../sys/include

.PHONY: all clean test

all: test

test_ps_state: test_ps_state.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $<

test: test_ps_state
	./test_ps_state

clean:
	rm -f test_ps_state
