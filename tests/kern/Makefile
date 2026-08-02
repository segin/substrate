# tests/kern/Makefile — kernel stress/soak tests.
#
# soak_reap: cumulative process/thread-reap leak + wedge soak.  Spawns and
# SIGKILLs many short-lived processes (a fraction blocked in a syscall), then
# reaps them, hammering the abnormal-exit / proc+thread teardown path.  It is
# the regression test for the concurrent-SIGKILL-reap wedge (a killed thread
# that re-blocked on a sleepq during teardown was resurrected from
# THREAD_ZOMBIE by a sleepq wake, re-scheduled, and busy-spun in proc_exit()'s
# post-yield while(1), wedging the whole guest).
#
# Modes (init arg):
#   soak_reap <iters>                 sequential fork+SIGKILL+reap
#   soak_reap batch:<n>:<thr>:<b>     <b> rounds of <n> concurrent victims
#                                     (<thr> threads each) SIGKILLed together
#   soak_reap wtest:<n>:<rounds>      waitpid(specific-pid) concurrency probe
#
# Cross build (static; run as init= under qemu):
#   make CROSS=/opt/substrate/bin/i386-unknown-substrate-
#   make CROSS=... THREADS=1        # link libpthread, threaded victims

CROSS   ?=
THREADS ?=

ifeq ($(CROSS),)
  CC      = cc
  CFLAGS  = -Wall -Wextra -O2 -g
  LIBS    = $(if $(THREADS),-pthread,)
  INC     =
else
  TOP     = ../..
  SR      = /opt/substrate/i386-unknown-substrate
  CC      = $(CROSS)gcc
  CFLAGS  = -m32 -std=c2x -nostdinc -fno-pie -no-pie -static -O2 -g -nostdlib \
            -march=i486 -I$(TOP)/include -I$(TOP)/sys/include -I$(TOP)/sys \
            -I$(TOP)/lib/pthread \
            -isystem $(shell $(CC) -m32 -print-file-name=include)
  CFLAGS += $(if $(THREADS),-DSOAK_THREADS,)
  CRT0    = $(SR)/lib/crt0.o
  PLIB    = $(if $(THREADS),-l:libpthread.a,)
  LIBS    = $(CRT0) -Wl,--start-group -L$(SR)/lib $(PLIB) \
            -l:libc.a -l:libm.a -l:libsys.a -Wl,--end-group \
            $(shell $(CC) -m32 -print-libgcc-file-name)
  # Brand OSABI = ELFOSABI_SUBSTRATE (0x40) so the native loader accepts it.
  BRAND   = printf '\100' | dd of=$@ bs=1 seek=7 conv=notrunc status=none
endif

all: soak_reap trapsig

soak_reap: soak_reap.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

trapsig: trapsig.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

clean:
	rm -f soak_reap trapsig

.PHONY: all clean
