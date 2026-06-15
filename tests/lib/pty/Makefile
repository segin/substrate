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
  CFLAGS += -I../../../include -I../../../sys
endif

# torture_pty uses pthreads to drive the blocking master<->slave
# wakeup paths from a second thread.
# Host build (no CROSS): -pthread links the system pthreads.
# Cross build (CROSS set): -l:libpthread.so.0 forces dynamic linking
# against substrate's libpthread.so.0 (no `libpthread.so` symlink is
# shipped in the source tree, so a plain -lpthread would fall through
# to the .a and bake the weak __ldso_alloc_tls ref to address 0).
ifeq ($(CROSS),)
  PTHREAD = -pthread
  PTHREAD_INC =
else
  PTHREAD = -L../../../lib/pthread -l:libpthread.so.0
  PTHREAD_INC = -I../../../lib/pthread
endif

TESTS = torture_pty torture_ctty torture_bsdpty

all: $(TESTS)

torture_pty: torture_pty.c
	$(CC) $(CFLAGS) $(PTHREAD_INC) $(LDFLAGS) $< $(PTHREAD) -o $@

torture_ctty: torture_ctty.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

torture_bsdpty: torture_bsdpty.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
