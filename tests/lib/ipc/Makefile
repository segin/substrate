# tests/lib/ipc/Makefile
#
# Portable POSIX test program for pipe/pipe2/mknod/mkfifo.  Builds on
# host or substrate; same conventions as tests/lib/sockets and
# tests/lib/pthread.
#
# Host build (default):
#     make
#     ./torture_ipc
#
# Substrate cross build (binary picked up by build-rootfs.sh
# install_to_dist and dropped at /tmp/torture_ipc on the image):
#     make CROSS=/opt/substrate/bin/i386-unknown-substrate-

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
  PTHREAD = -L../../../lib/pthread -l:libpthread.so.0
  PTHREAD_INC = -I../../../include -I../../../sys -I../../../lib/pthread
else
  PTHREAD = -pthread
  PTHREAD_INC =
endif

TESTS = torture_ipc torture_pipe torture_sem

all: $(TESTS)

torture_ipc: torture_ipc.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(PTHREAD_INC) $< $(PTHREAD) -o $@

torture_pipe: torture_pipe.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(PTHREAD_INC) $< $(PTHREAD) -o $@

# torture_sem uses only fork/semop — no pthread.
torture_sem: torture_sem.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(PTHREAD_INC) $< -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
