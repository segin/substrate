CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-pointer-to-int-cast -DHOST_TEST
INCLUDES = -I../../../bin/ps -I../../../sys -I../../../sys/include
PS_SRCS = ../../../bin/ps/ps_parse.c ../../../bin/ps/ps_format.c

.PHONY: all clean test

all: test

test_ps_state: test_ps_state.c $(PS_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(PS_SRCS)

# Option parsing: flags, and the -p/--ppid/-U/operand selection lists.
test_ps: test_ps.c $(PS_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(PS_SRCS)

test: test_ps test_ps_state
	./test_ps
	./test_ps_state

clean:
	rm -f test_ps_state test_ps
