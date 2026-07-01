# tests/lib/mqueue/Makefile — POSIX message-queue functional test.
#
# Builds a statically-linked substrate-native binary (branded OSABI=64) so it
# can be run directly as init= under run-auto-test.sh:
#
#     make -C tests/lib/mqueue
#     # inject tests/lib/mqueue/mqtest into a rootfs.img copy and boot
#     #   init=/tmp/mqtest
#
# Links the in-tree librt.a (mq_* wrappers) alongside libc/libm/libpthread/
# libsys, exactly like the main userland static-link recipe.

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

all: mqtest

mqtest: mqtest.o
	$(CC) $(LDFLAGS) -o $@ mqtest.o $(CRT0) $(LIBS)
	$(call BRAND_SUBSTRATE,$@)

mqtest.o: mqtest.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f mqtest mqtest.o

.PHONY: all clean
