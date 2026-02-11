CC = gcc
CFLAGS = -Wall -Wextra -g -I../sys -I../sys/arch/i386 -I../sys/kern -I../sys/drivers/video -I../sys/exec/perso -I../sys/include -DHOST_TEST
CFLAGS_HOST = -Wall -Wextra -g -I../sys -I../sys/arch/i386 -I../sys/kern -I../sys/drivers/video -I../sys/exec/perso -idirafter ../sys/include -DHOST_TEST

# Core kernel sources for testing
CORE_SRCS = ../sys/vm/vm_page.c vm_map_host.c ../sys/vm/vm_object.c \
          ../sys/vm/vm_fault.c ../sys/vm/vm_syscalls.c ../sys/vm/vm_swap.c \
          ../sys/vm/vm_zone.c \
          ../sys/kern/spinlock.c ../sys/kern/mutex.c ../sys/kern/semaphore.c ../sys/kern/kthread.c ../sys/kern/time.c ../sys/kern/signal.c ../sys/kern/futex.c \
          ../sys/lib/string.c ../sys/lib/printf.c ../sys/lib/div64.c \
          ../sys/exec/perso/compat.c ../sys/kern/acct.c ../sys/kern/sysinfo.c \
          ../sys/vfs/vfs.c ../sys/arch/i386/syscall.c ../sys/arch/i386/sched.c

UNIT_TESTS = unit/vm/test_kmem.c unit/vm/test_zone.c unit/vm/test_map.c \
             unit/vm/test_object.c unit/vm/test_page.c unit/vm/test_fault.c \
             unit/vm/test_user_memory.c unit/vm/test_swap.c unit/vm/test_cow.c \
             unit/arch/test_trampoline.c \
             unit/kern/test_spinlock.c unit/kern/test_kthread.c unit/kern/test_sched.c \
             unit/kern/test_sync.c unit/kern/test_signal.c \
             unit/vfs/test_fd.c unit/vfs/test_permissions.c unit/vfs/test_chroot.c \
             unit/kern/prop_sched.c fuzz/fuzz_sched.c \
             unit/perso/test_att.c ../sys/exec/perso/perso_svr3.c ../sys/exec/perso/perso_svr4.c \
             sys/test_sysinfo.c unit/lib/test_string.c

MOCKS = mocks.c

all: test_runner test_minix test_ext2_perf test_ext2_remove_perf
	./test_runner
	./test_minix
	./test_ext2_perf
	./test_ext2_remove_perf

test_ext2_perf: unit/fs/test_ext2_perf_host.c
	$(CC) $(CFLAGS_HOST) $< -o $@

test_ext2_remove_perf: unit/fs/test_ext2_remove_perf.c
	$(CC) $(CFLAGS_HOST) $< -o $@

test_minix: unit/fs/test_minix_standalone.c
	$(CC) $(CFLAGS) $< -o $@

test_runner: $(CORE_SRCS) $(UNIT_TESTS) $(MOCKS) test_runner.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f test_runner test_minix test_ext2_perf