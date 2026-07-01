# tests/lib/aio/Makefile — POSIX AIO functional test.
#
# Builds a statically-linked substrate-native binary (branded OSABI=64) so it
# can be run directly as init= under run-auto-test.sh:
#
#     make -C tests/lib/aio
#     # inject tests/lib/aio/aiotest into a rootfs.img copy and boot
#     #   init=/tmp/aiotest
#
# The aio_* implementation lives in librt and uses libpthread's worker pool.

include ../../../Makefile.inc

CFLAGS  := $(USER_CFLAGS)
LDFLAGS := $(USER_LDFLAGS)

CRT0 = $(TOP)/lib/c/crt0.o
LIBS = -Wl,--start-group \
       -L$(TOP)/lib/c -lc \
       -L$(TOP)/lib/m -lm \
       -L$(TOP)/lib/rt -l:librt.a \
       -L$(TOP)/lib/pthread -l:libpthread.a \
       -L$(TOP)/lib/sys -l:libsys.a \
       -Wl,--end-group $(LIBGCC)

all: aiotest

aiotest: aiotest.o
	$(CC) $(LDFLAGS) -o $@ aiotest.o $(CRT0) $(LIBS)
	$(call BRAND_SUBSTRATE,$@)

aiotest.o: aiotest.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f aiotest aiotest.o

.PHONY: all clean
