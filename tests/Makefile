CC = gcc
CFLAGS_COMMON = -Wall -Wextra -g -DHOST_TEST -D_GNU_SOURCE
CFLAGS_KERN = $(CFLAGS_COMMON) -I../sys -I../sys/arch/i386 -I../sys/drivers/video -I../sys/exec/perso -I../sys/include
CFLAGS_TEST = $(CFLAGS_COMMON) -I../sys/include -I../sys -I. -DNO_MAIN

# Core kernel objects for testing
CORE_OBJS = vm_page.o vm_map_host.o vm_object.o vm_fault.o vm_syscalls.o vm_pager.o vm_swap.o vm_zone.o \
            spinlock.o mutex.o semaphore.o kthread.o time.o signal.o futex.o sleepq.o \
            string.o printf.o div64.o compat.o acct.o sysinfo.o \
            vfs.o vfs_cache.o vnode.o vnode_ops.o syscall.o sched_i386.o sched_pm.o process.o wait.o sched_decay.o libc_wrappers.o \
            pipe.o ansi_handler.o

# Unit tests
UNIT_SRCS = unit/drivers/console/test_ansi.c unit/vm/test_kmem.c unit/vm/test_zone.c unit/vm/test_map.c \
             unit/vm/test_object.c unit/vm/test_page.c unit/vm/test_fault.c \
             unit/vm/test_user_memory.c unit/vm/test_swap.c unit/vm/test_swap_real.c unit/vm/test_cow.c \
             unit/arch/test_trampoline.c \
             unit/kern/test_spinlock.c unit/kern/test_kthread.c unit/kern/test_sched.c unit/kern/test_sched_decay.c \
             unit/kern/test_sync.c unit/kern/test_sleepq.c unit/kern/test_signal.c unit/kern/test_pthread_exit.c \
             unit/kern/test_reboot.c \
             unit/vfs/test_fd.c unit/vfs/test_permissions.c unit/vfs/test_chroot.c unit/vfs/test_readdir.c \
             unit/vfs/test_link.c unit/vfs/test_rename.c unit/vfs/test_symlink.c unit/vfs/test_readlink.c \
             unit/fs/test_fuse.c \
             unit/kern/prop_sched.c fuzz/fuzz_sched.c \
             unit/perso/test_att.c ../sys/exec/perso/perso_svr3.c ../sys/exec/perso/perso_svr4.c \
             sys/test_sysinfo.c lib/c/test_libc_string_unit.c unit/lib/test_time.c \
             sys/test_div64.c ../tests/sys/test_pipe_race.c sys/test_mmap_batch.c unit/fs/test_procfs.c

UNIT_OBJS = $(notdir $(UNIT_SRCS:.c=.o))

MOCKS_OBJ = mocks.o

all: test_runner test_minix test_ext2_perf test_ext2_read_blocks test_ext2_find_next_zero_bit test_ext2_inode_write test_libc_string test_libc_time test_libc_stdlib test_libc_ctype test_libc_assert test_libc_sysctl test_libsys_getuid test_ext2_remove_perf bench_ext2_readdir test_ext2_write_block test_libpthread_mutex test_pthread_join test_readline test_history test_bc test_libedit host_test_wait_logic host_test_pgrp host_test_proc_tree host_test_proc_exit host_test_tty_jobctl host_test_sync_guards host_test_sleepq_private host_test_swapper host_test_rusage host_test_proc_lifecycle host_test_i386_signal host_test_linux_signal host_test_signal_restart host_test_signal_integration host_test_proc_signal_state host_test_rwlock host_test_fork_exec_wait
	./test_runner
	./test_minix
	./test_ext2_find_next_zero_bit
	./test_ext2_perf
	./test_ext2_read_blocks
	./test_ext2_find_next_zero_bit
	./test_ext2_inode_write
	./test_libc_string
	./test_libc_time
	./test_libc_stdlib
	./test_libc_ctype
	./test_libc_assert
	./test_libc_sysctl
	./test_libsys_getuid
	./test_ext2_remove_perf
	./bench_ext2_readdir
	./test_ext2_write_block
	./test_libpthread_mutex
	./test_pthread_join
	$(MAKE) -C lib/edit
	./lib/edit/host_test_history
	./test_readline
	./test_libedit
	./test_bc
	./host_test_wait_logic
	./host_test_pgrp
	./host_test_proc_tree
	./host_test_proc_exit
	./host_test_tty_jobctl
	./host_test_sync_guards
	./host_test_sleepq_private
	./host_test_swapper
	./host_test_rusage
	./host_test_proc_lifecycle
	./host_test_i386_signal
	./host_test_linux_signal
	./host_test_signal_restart
	./host_test_signal_integration
	./host_test_proc_signal_state
	./host_test_rwlock
	./host_test_fork_exec_wait

test_readline: lib/edit/test_readline.c ../lib/edit/readline.c
	$(CC) $(CFLAGS_COMMON) -I../lib/edit $< -o $@

test_libedit: lib/edit/test_editline.c ../lib/edit/editline.c
	$(CC) $(CFLAGS_COMMON) -I../include -I../lib/edit $< ../lib/edit/editline.c -o $@

host_test_wait_logic: sys/host_test_wait_logic.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_pgrp: sys/host_test_pgrp.c ../sys/pm/pgrp.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_proc_tree: sys/host_test_proc_tree.c ../sys/pm/process.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_proc_exit: sys/host_test_proc_exit.c ../sys/pm/process.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_tty_jobctl: sys/host_test_tty_jobctl.c ../sys/drivers/console/tty.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_sync_guards: sys/host_test_sync_guards.c ../sys/kern/mutex.c ../sys/kern/semaphore.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_rwlock: sys/host_test_rwlock.c ../sys/kern/rwlock.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_sleepq_private: sys/host_test_sleepq_private.c ../sys/kern/sleepq.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_swapper: sys/host_test_swapper.c ../sys/kern/swapper.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_rusage: sys/host_test_rusage.c ../sys/pm/rusage.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_proc_lifecycle: sys/host_test_proc_lifecycle.c ../sys/pm/process.c ../sys/pm/wait.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_i386_signal: sys/host_test_i386_signal.c ../sys/arch/i386/signal.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_linux_signal: sys/host_test_linux_signal.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_signal_restart: sys/host_test_signal_restart.c ../sys/kern/signal.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_signal_integration: sys/host_test_signal_integration.c ../sys/kern/signal.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_proc_signal_state: sys/host_test_proc_signal_state.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_fork_exec_wait: sys/host_test_fork_exec_wait.c
	$(CC) $(CFLAGS_KERN) $< -o $@

# Duplicate rule removed

test_pthread_join: lib/test_pthread_join.c ../lib/pthreads/pthread.c
	$(CC) $(CFLAGS_COMMON) -Wno-int-conversion -Wno-pointer-to-int-cast -I../include -I../sys/include -I../sys $< -o $@

test_libpthread_mutex: lib/test_pthread_mutex.c ../lib/pthreads/pthread.c
	$(CC) $(CFLAGS_COMMON) -I../include -I../sys/include -I../sys $< -o $@

test_history:
	$(MAKE) -C lib/edit

test_libc_assert: lib/c/test_assert.c ../lib/c/src/assert.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin lib/c/test_assert.c -o $@

test_libc_sysctl: lib/c/test_sysctl.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin -I../include lib/c/test_sysctl.c -o $@

test_libsys_getuid: lib/sys/test_getuid.c ../lib/sys/getuid.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin -I../include -I../sys/include -I../sys lib/sys/test_getuid.c -o $@

test_libc_ctype: lib/c/test_ctype.c ../lib/c/src/ctype.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin lib/c/test_ctype.c -o $@

test_ext2_perf: unit/fs/test_ext2_perf_host.c
	$(CC) $(CFLAGS_KERN) $< -o $@

test_ext2_read_blocks: unit/fs/test_ext2_read_blocks_host.c
	$(CC) $(CFLAGS_KERN) $< -o $@

test_ext2_find_next_zero_bit: unit/fs/test_ext2_find_next_zero_bit.c
	$(CC) $(CFLAGS_KERN) $< -o $@

test_ext2_inode_write: unit/fs/test_ext2_inode_write_host.c
	$(CC) $(CFLAGS_KERN) $< -o $@

bench_ext2_readdir: unit/fs/bench_ext2_readdir_host.c
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
%.o: ../sys/fs/%.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@
%.o: ../sys/pm/%.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@

%.o: ../sys/drivers/console/%.c
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
%.o: unit/drivers/console/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: unit/fs/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: unit/perso/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: fuzz/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
%.o: sys/%.c
	$(CC) $(CFLAGS_TEST) -c $< -o $@
test_pipe_race.o: ../tests/sys/test_pipe_race.c
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
	rm -f *.o test_runner test_minix test_ext2_perf test_ext2_read_blocks test_ext2_find_next_zero_bit test_ext2_inode_write test_libc_string test_libc_time test_libc_stdlib test_libc_ctype test_libc_assert test_libc_sysctl test_libsys_getuid test_ext2_remove_perf bench_ext2_readdir test_ext2_write_block test_libpthread_mutex test_pthread_join test_readline test_libedit test_bc host_test_wait_logic host_test_pgrp host_test_proc_tree host_test_proc_exit host_test_tty_jobctl host_test_sync_guards host_test_sleepq_private host_test_swapper host_test_rusage host_test_proc_lifecycle host_test_i386_signal host_test_signal_restart host_test_signal_integration host_test_rwlock host_test_fork_exec_wait
	$(MAKE) -C sys clean
	$(MAKE) -C lib/edit clean

bench_vm_map: bench_vm_map.o mocks_vm.o vm_map.o
	$(CC) $(CFLAGS_KERN) $^ -o $@

bench_vm_map.o: bench_vm_map.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@

bench_vm_map_alloc: bench_vm_map_alloc.o mocks_vm.o vm_map.o
	$(CC) $(CFLAGS_KERN) $^ -o $@

bench_vm_map_alloc.o: bench_vm_map_alloc.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@

mocks_vm.o: mocks_vm.c
	$(CC) $(CFLAGS_KERN) -c $< -o $@

test_bc: bin/bc/host_test_bc.c ../bin/bc/bc.c ../usr.lib/bc/num.c ../usr.lib/bc/arith.c
	$(CC) $(CFLAGS_COMMON) $< ../usr.lib/bc/num.c ../usr.lib/bc/arith.c -o $@

test_ext2_write_block: unit/fs/test_ext2_write_block_host.c
	$(CC) $(CFLAGS_KERN) $< -o $@
