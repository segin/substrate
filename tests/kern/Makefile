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
  RTLIBS  = -lrt -pthread
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
  # librt (mq_*) and libpthread (sem_*), for the IPC tests.
  RTLIBS  = $(CRT0) -Wl,--start-group -L$(SR)/lib -l:librt.a -l:libpthread.a \
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

# chownperm: chown's POSIX permission rules (group membership, no giving
# away, setuid/setgid cleared) and the change reaching ext2.  Needs root;
# runs on the host too.
chownperm: chownperm.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# chrootesc: absolute symlinks inside a chroot resolve inside it, natively
# and (chrootesc.linux, static i386 Linux) under a personality.  Needs
# root; runs on the host too.
chrootesc: chrootesc.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

chrootesc.linux: chrootesc.c
	gcc -m32 -static -O2 $< -o $@

# linuxthrexit.linux: a glibc program's threads end one at a time (exit(2)
# ended the process) and pthread_join returns (the clear-tid word was set to
# 1, not 0).  Runs on the host too.
linuxthrexit.linux: linuxthrexit.c
	gcc -m32 -static -O2 -pthread $< -o $@

# linkfollow.linux: link(2) links a final symlink itself, linkat(2) follows
# it only with AT_SYMLINK_FOLLOW, and no link crosses filesystems.  Runs on
# the host too, and builds on FreeBSD.
linkfollow.linux: linkfollow.c
	gcc -m32 -static -O2 $< -o $@

# execipc: exec drops shm attachments, mq descriptors with their mq_notify
# registration, and named semaphores.  Substrate only.
execipc: execipc.c
	$(CC) $(CFLAGS) $< $(RTLIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# lseekchk: lseek refuses a position before the start or past the largest
# off_t, and keeps 64-bit positions.  Runs on the host too.
lseekchk: lseekchk.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# execfail: an exec that fails part way through loading returns to a caller
# whose memory is intact, and does not leak.  One broken program per loader,
# made from a good one (see the usage comment in execfail.c), e.g.
#   execfail x286-entry /perso/xenix286s/bin/echo /perso/xenix286s/bin/echo
#   execfail x386-eseg  /perso/xenix/bin/ls     (a 386 x.out there)
#   execfail truncate   /hello_elks              (tests/elks)
#   execfail env        /perso/linux/usr/bin/printenv   (a.out)
#   execfail env        /perso/svr3/bin/echo            (COFF)
execfail: execfail.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# aoutstack: the a.out initial stack is sized to argv/envp, and an exec whose
# arguments are too large fails with E2BIG instead of writing past it.
# Needs an a.out or COFF echo (both use aout_build_stack): run as
# `aoutstack /perso/svr3/bin/echo` with svr3-323.img mounted.
aoutstack: aoutstack.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# aoutrun: a Linux ZMAGIC a.out (text at address 0) runs and prints.
# Run as `aoutrun /perso/linux/usr/bin/printenv` with Slackware 1.1.2 mounted.
aoutrun: aoutrun.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# procnodes: /proc nodes held open stay those of what was opened, across
# the reaping of the process and lookups of others; WaitChan is redacted for
# readers who may not inspect.  Run as root.  Runs on the host too.
procnodes: procnodes.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# forkleak: 300 fork+exec+exit+reap rounds give their memory back (x86_64
# leaked every pv entry: ~40 KB a round).  Guest only: a busy host's
# MemFree is too noisy.
forkleak: forkleak.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# rmdirwalk: path walks through a directory being removed (and finished
# off at its last close) neither crash the kernel nor wedge.  Runs on the
# host too.
rmdirwalk: rmdirwalk.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# thrwake: thr_wake reaches only the caller's own threads.  Substrate only.
thrwake: thrwake.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# getdentspad.linux: getdents64 entries carry no kernel stack in their
# padding.  Runs on the host too.
getdentspad.linux: getdentspad.c
	gcc -m32 -static -O2 $< -o $@

# sysctlstr: a string sysctl takes a value without its NUL, and an empty one.
# Substrate only.
sysctlstr: sysctlstr.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# acctpath.linux: acct(2) takes a whole path, and only a regular file.
acctpath.linux: acctpath.c
	gcc -m32 -static -O2 $< -o $@

# ipcgetperm.linux: shmget/semget on an existing key check only the access
# asked for.  Runs on the host too (as root).
ipcgetperm.linux: ipcgetperm.c
	gcc -m32 -static -O2 $< -o $@

# slabsredact: sys_vm_slabs shows kernel addresses to root only.  Substrate
# only; run as root.
slabsredact: slabsredact.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# setidneg.linux: setuid(-1) and setgid(-1) are EINVAL.  Runs on the host
# too (as root).
setidneg.linux: setidneg.c
	gcc -m32 -static -O2 $< -o $@

# semundoset.linux: SETVAL/SETALL discard pending SEM_UNDO adjustments.
# Runs on the host too.
semundoset.linux: semundoset.c
	gcc -m32 -static -O2 $< -o $@

# longtimeout: far-off futex and mq timeouts wait; a negative relative one
# is EINVAL.  Substrate only.
longtimeout: longtimeout.c
	$(CC) $(CFLAGS) $< $(RTLIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# procinspect: sys_proc_* show another user's or a set-id process only to
# root.  Substrate only; run as root.
procinspect: procinspect.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# seclevel: kern.securelevel cannot be lowered once above zero.  Substrate
# only; leaves the level raised until reboot.
seclevel: seclevel.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# bigtrunc: shrinking a file of 4 GiB or more keeps the right size (ext2's
# i_size_high).  Runs on the host too.
bigtrunc: bigtrunc.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# tlsstatic: a statically linked program's __thread variables, in its main
# thread and its threads, and libpthread's own TLS.  Runs on the host too.
tlsstatic: tlsstatic.c
	$(CC) $(CFLAGS) $< $(RTLIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# ptsrace: /dev/pts/N looked up while ptys are made and destroyed.  Runs on
# the host too.
ptsrace: ptsrace.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# walkrecycle: a path walk keeps the nodes it passes through while other
# lookups cycle the ext2 node cache.  Runs on the host too.
walkrecycle: walkrecycle.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# unlinkread: files read, unlinked while open and closed give back their
# memory (ext2 leaked the node slot's 4 KiB of read buffers; the block
# cache grew new headers while empty ones sat unused).  Guest only.
unlinkread: unlinkread.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# fatops: on a FAT mount, an empty file truncated larger reads zeros, a
# case-only rename keeps the file, and a write at 4 GiB does not wrap to 0.
# Run as `fatops <dir on FAT>`, then fsck.fat -n the image.
fatops: fatops.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# pipeleak: a pipe() that fails part way, or whose descriptors cannot be
# copied out, leaves nothing allocated or open.  Runs on the host too.
pipeleak: pipeleak.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# cowkeep: a page made private by a COW fault keeps its contents across
# later forks.  Runs on the host too.
cowkeep: cowkeep.c
	$(CC) $(CFLAGS) $< $(LIBS) -o $@
	$(if $(BRAND),$(BRAND),)

# sigretsel.linux: sigreturn kills a process whose handler planted a bad
# selector (instead of faulting the kernel), keeps SIGKILL and SIGSTOP
# unblockable, and keeps Linux signal numbers in the saved mask.  A static
# i386 Linux program; runs on the host too.
sigretsel.linux: sigretsel.c
	gcc -m32 -static -O2 $< -o $@

clean:
	rm -f soak_reap trapsig advlock opencreat mmapshared cowwrite pokecow brkfork \
	      brkfork.linux thrstart vm86guest thrkill tidkern.linux truncperm shmgrow \
	      ioctlkern.linux ioctlkern_fbsd linuxptr.linux setredrop shmrange rofault \
	      sigretsel.linux execipc linkfollow.linux walkrecycle ptsrace

.PHONY: all clean
