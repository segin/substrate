# tests/bin/telnetd — end-to-end self-execing torture test for the
# telnet client / telnetd server / login-child chain.  Builds in two
# flavours:
#
#   make                 host build (gcc + libutil) for sanity-checking
#                        the logic on Linux.
#   make CROSS=…         substrate cross build for running on the image.
#
# The host build links -lutil for openpty/login_tty; the substrate
# build picks them up from libc.so.0 via DT_NEEDED.

PROG = torture_telnetd

ifneq ($(CROSS),)
CC      = $(CROSS)gcc
CFLAGS  = -Wall -Wextra -O0 -g
LDFLAGS =
else
CC      ?= cc
CFLAGS  = -DNATIVE_BUILD=1 -Wall -Wextra -O0 -g
LDFLAGS = -lutil
endif

all: $(PROG)

$(PROG): $(PROG).c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(PROG) $(PROG).o
