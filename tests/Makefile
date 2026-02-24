CC = gcc
CFLAGS_COMMON = -Wall -Wextra -g -DHOST_TEST -D_GNU_SOURCE
CFLAGS_KERN = $(CFLAGS_COMMON) -I../sys -I../sys/arch/i386 -I../sys/drivers/video -I../sys/exec/perso -I../sys/include
CFLAGS_TEST = $(CFLAGS_COMMON) -I../sys/include -I../sys -I. -DNO_MAIN

# Core kernel objects for testing
CORE_OBJS = vm_page.o vm_map_host.o vm_object.o vm_fault.o vm_syscalls.o vm_swap.o vm_zone.o \
            spinlock.o mutex.o semaphore.o kthread.o time.o signal.o futex.o sleepq.o \
            string.o printf.o div64.o compat.o acct.o sysinfo.o \
            vfs.o vfs_cache.o vnode.o vnode_ops.o syscall.o sched_i386.o sched_pm.o process.o wait.o sched_decay.o libc_wrappers.o

# Unit tests
UNIT_SRCS = unit/vm/test_kmem.c unit/vm/test_zone.c unit/vm/test_map.c \
             unit/vm/test_object.c unit/vm/test_page.c unit/vm/test_fault.c \
             unit/vm/test_user_memory.c unit/vm/test_swap.c unit/vm/test_cow.c \
             unit/vm/test_mmap_cleanup.c \
             unit/arch/test_trampoline.c \
             unit/kern/test_spinlock.c unit/kern/test_kthread.c unit/kern/test_sched.c unit/kern/test_sched_decay.c \
             unit/kern/test_sync.c unit/kern/test_sleepq.c unit/kern/test_signal.c unit/kern/test_pthread_exit.c \
             unit/kern/test_reboot.c \
             unit/vfs/test_fd.c unit/vfs/test_permissions.c unit/vfs/test_chroot.c unit/vfs/test_readdir.c \
             unit/vfs/test_link.c unit/vfs/test_rename.c unit/vfs/test_symlink.c unit/vfs/test_readlink.c \
             unit/kern/prop_sched.c fuzz/fuzz_sched.c \
             unit/perso/test_att.c ../sys/exec/perso/perso_svr3.c ../sys/exec/perso/perso_svr4.c \
             sys/test_sysinfo.c lib/c/test_libc_string_unit.c unit/lib/test_time.c \
             sys/test_div64.c

UNIT_OBJS = $(notdir $(UNIT_SRCS:.c=.o))

MOCKS_OBJ = mocks.o

all: test_runner test_minix test_ext2_perf test_libc_string test_libc_time test_libc_stdlib test_libc_ctype test_ext2_remove_perf
	./test_runner
	./test_minix
	./test_ext2_perf
	./test_libc_string
	./test_libc_time
	./test_libc_stdlib
	./test_libc_ctype
	./test_ext2_remove_perf

# Duplicate rule removed

test_libc_ctype: lib/c/test_ctype.c ../lib/c/src/ctype.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin lib/c/test_ctype.c -o $@

test_ext2_perf: unit/fs/test_ext2_perf_host.c
	$(CC) $(CFLAGS_KERN) $< -o $@

test_ext2_remove_perf: unit/fs/test_ext2_remove_perf.c
	$(CC) $(CFLAGS_KERN) $< -o $@

test_minix: unit/fs/test_minix_standalone.c
	$(CC) $(CFLAGS_KERN) $< -o $@

test_libc_string: lib/c/test_libc_string_unit.c ../lib/c/src/string.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin -c ../lib/c/src/string.c -o string_raw.o
	objcopy --prefix-symbols=libc_ string_raw.o string_prefixed.o
	$(CC) $(CFLAGS_COMMON) lib/c/test_libc_string_unit.c string_prefixed.o libc_wrappers.o -o $@

test_libc_time: lib/c/test_time_standalone.c ../lib/c/src/time/time.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin lib/c/test_time_standalone.c -o $@

test_libc_stdlib: lib/c/test_stdlib.c ../lib/c/src/stdlib.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin lib/c/test_stdlib.c -o $@

# Rules for objects
%.o: ../sys/vm/%.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@
%.o: ../sys/kern/%.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@
%.o: ../sys/lib/%.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@
%.o: ../sys/exec/perso/%.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@
%.o: ../sys/vfs/%.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@
%.o: ../sys/pm/%.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@

sched_i386.o: ../sys/arch/i386/sched.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@

sched_pm.o: ../sys/pm/sched.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@

vm_map_host.o: vm_map_host.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@

# Unit test objects
%.o: unit/vm/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: unit/arch/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: unit/kern/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: unit/vfs/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: unit/perso/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: fuzz/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: sys/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
test_libc_string_unit.o: lib/c/test_libc_string_unit.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@

%.o: lib/c/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: unit/lib/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@

libc_wrappers.o: libc_wrappers.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@

mocks.o: mocks.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@

test_runner.o: test_runner.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@

test_runner: test_runner.o $(CORE_OBJS) $(UNIT_OBJS) $(MOCKS_OBJ) libc_wrappers.o string_prefixed.o
	$(CC) $(CFLAGS_COMMON) $^ -o $@

string_prefixed.o: ../lib/c/src/string.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin -c ../lib/c/src/string.c -o string_raw.o
	objcopy --prefix-symbols=libc_ string_raw.o string_prefixed.o

clean:
	rm -f *.o test_runner test_minix test_ext2_perf test_libc_string test_libc_time test_libc_stdlib test_libc_ctype test_ext2_remove_perf
	$(MAKE) -C sys clean
