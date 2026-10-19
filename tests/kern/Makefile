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

all: soak_reap trapsig advlock opencreat mmapshared cowwrite pokecow brkfork thrstart \
     vm86guest thrkill truncperm shmgrow setredrop shmrange rofault

soak_reap: soak_reap.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

trapsig: trapsig.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# advlock: fcntl(2) record locks are on the file and are a process's own;
# F_SETLKW waits.  Runs on the host too, where it must also pass.
advlock: advlock.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# opencreat: open(O_CREAT) of a file several processes create at once fails
# for none of them; with O_EXCL, for all but one.  Runs on the host too.
opencreat: opencreat.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# mmapshared: what is stored through a MAP_SHARED mapping of a file is in
# the file, however the mapping is let go of.  Runs on the host too.
mmapshared: mmapshared.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# cowwrite: the kernel's stores into user memory (read(2) into a buffer)
# respect copy-on-write and read-only pages.  Runs on the host too.
cowwrite: cowwrite.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# pokecow: a ptrace POKE into data or text the tracee shares (with its
# parent after fork, or with every process running the same file)
# changes the tracee's copy only.  Runs on the host too.
pokecow: pokecow.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# brkfork: shrinking the heap does not free a page a process forked from
# this one still maps.  Runs on the host too; brkfork.linux, a static
# i386 Linux build, checks the same through the Linux personality.
brkfork: brkfork.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

brkfork.linux: brkfork.c
	gcc -m32 -static -O2 $< -o $@

# tidkern.linux: the exit store set_tid_address arranges never reaches
# kernel memory.  A static i386 Linux program (set_tid_address is a Linux
# call); takes the kernel_hostname address of the kernel under test.
tidkern.linux: tidkern.c
	gcc -m32 -static -O2 $< -o $@

# ioctlkern.linux: a user's tty ioctl argument is never used as a kernel
# buffer.  A static i386 Linux program; takes the kernel_hostname address.
ioctlkern.linux: ioctlkern.c
	gcc -m32 -static -O2 $< -o $@

# linuxptr.linux: the Linux personality's calls copy user memory in and
# out.  A static i386 Linux program; takes the kernel_hostname address.
linuxptr.linux: linuxptr.c
	gcc -m32 -static -O2 $< -o $@

# ioctlkern_fbsd: the same through the FreeBSD personality (also NetBSD's
# ioctl).  Freestanding, branded ELFOSABI_FREEBSD (9).
ioctlkern_fbsd: ioctlkern_fbsd.c
	gcc -m32 -static -nostdlib -ffreestanding -fno-builtin -fno-pie -no-pie \
	    -fno-stack-protector -O2 $< -o $@
	printf '\011' | dd of=$@ bs=1 seek=7 conv=notrunc status=none

# thrstart: thr_new with a stack the caller cannot write fails EFAULT
# instead of panicking, and a thread with no TLS block starts.  Substrate
# only.
thrstart: thrstart.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# vm86guest: a VM86 guest the monitor cannot carry on with is ended by a
# signal, and gets no IOPL or NT however it asks.  Substrate i386 only.
vm86guest: vm86guest.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# thrkill: thr_kill2 follows kill(2)'s permission rule and init's
# protection.  Run as root.  Substrate only.
thrkill: thrkill.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# truncperm: truncate(2) needs write permission and refuses a directory.
# Run as root (on the host too).
truncperm: truncperm.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# shmgrow: growing a mapped shm object keeps the frames the mapping
# uses.  Runs on the host too (-lrt).
shmgrow: shmgrow.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# setredrop: setreuid(getuid(), getuid()) in a set-user-ID program drops
# the saved uid too (likewise setregid).  Needs root; runs on the host too.
setredrop: setredrop.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# shmrange: shmat at an explicit address refuses a segment that would run
# past the top of user space or wrap.  Runs on the host too.
shmrange: shmrange.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# rofault: a PI futex on a read-only page and a path string running into an
# unmapped page fail with EFAULT instead of looping on the fault.
# Substrate-only (native SYS_FUTEX).
rofault: rofault.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

clean:
	rm -f soak_reap trapsig advlock opencreat mmapshared cowwrite pokecow brkfork \
	      brkfork.linux thrstart vm86guest thrkill tidkern.linux truncperm shmgrow \
	      ioctlkern.linux ioctlkern_fbsd linuxptr.linux setredrop shmrange rofault

.PHONY: all clean
