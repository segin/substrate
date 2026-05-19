# tests/lib/sockets/Makefile
#
# Portable POSIX test program for AF_UNIX SOCK_STREAM.  Builds and
# runs on Linux / FreeBSD / macOS with the host toolchain; cross-
# builds against substrate's libc + libpthread when CROSS is set.
#
# Host build (default):
#     make
#     ./torture_unix
#
# Substrate cross build (drops binary at ./torture_unix and is
# picked up by build-rootfs.sh for /tmp on the image):
#     make CROSS=/opt/substrate/bin/i386-unknown-substrate-

CROSS  ?=
# Substrate's stage-1 toolchain ships $(CROSS)gcc; host toolchains
# usually have plain `cc`.  Pick the right driver automatically.
ifeq ($(CROSS),)
  CC      = cc
else
  CC      = $(CROSS)gcc
endif
CFLAGS  = -Wall -Wextra -O2 -g

# Substrate cross-build: match the userland baseline (i486+) so
# 4-byte atomics inline rather than calling __sync_*_4 stubs that
# substrate libgcc.a doesn't ship.  Force dynamic linking against
# libpthread.so.0 — same rationale as tests/lib/pthread/Makefile.
ifneq ($(CROSS),)
  CFLAGS += -march=i486 -mtune=i486
  LDFLAGS += -Wl,--copy-dt-needed-entries -Wl,-rpath-link,/opt/substrate/i386-unknown-substrate/lib
  PTHREAD = -L../../../lib/pthread -l:libpthread.so.0
  # -I../../../sys is needed because <sys/syscall.h> in our
  # userspace tree pulls in <arch/i386/syscall.h>, which lives at
  # sys/arch/i386/syscall.h (shared between kernel and userland).
  PTHREAD_INC = -I../../../include -I../../../sys -I../../../lib/pthread
else
  PTHREAD = -pthread
  PTHREAD_INC =
endif

TESTS = torture_unix

all: $(TESTS)

torture_unix: torture_unix.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(PTHREAD_INC) $< $(PTHREAD) -o $@

clean:
	rm -f $(TESTS)

.PHONY: all clean
