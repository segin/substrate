CC = gcc
CFLAGS_COMMON = -Wall -Wextra -g -DHOST_TEST -D_GNU_SOURCE

# Substrate's own libc sources, when compiled natively so the host tests can
# exercise them, meet glibc's headers rather than substrate's.  _GNU_SOURCE
# then makes glibc declare the GNU flavour of functions substrate implements
# to POSIX, and the definitions collide: strerror_r is the loud one, declared
# `char *` by glibc against substrate's POSIX `int`.  Ask for POSIX 2008,
# which is what these sources are actually written to.  Only for the
# lib/c/src/*.c compiles that lack substrate's -I paths -- the rules that do
# pass them pick up substrate's headers and need no help.  _DEFAULT_SOURCE
# rides along for the BSD extensions these sources use (arc4random_uniform);
# it does not bring back the GNU strerror_r, which needs _GNU_SOURCE proper.
CFLAGS_LIBC_HOST = $(filter-out -D_GNU_SOURCE,$(CFLAGS_COMMON)) \
                   -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CFLAGS_KERN = $(CFLAGS_COMMON) -I../sys -I../sys/arch/i386 -I../sys/drivers/video -I../sys/exec/perso -I../sys/include
CFLAGS_TEST = $(CFLAGS_COMMON) -I../sys/include -I../sys -I. -DNO_MAIN

# Core kernel objects for testing
CORE_OBJS = vm_page.o vm_map_host.o vm_object.o vm_fault.o vm_syscalls.o vm_pager.o vm_swap.o vm_zone.o \
            spinlock.o mutex.o semaphore.o kthread.o time.o signal.o futex.o sleepq.o \
            string.o printf.o div64.o compat.o acct.o sysinfo.o \
            vfs.o vfs_cache.o vfs_lookup.o vnode.o vnode_ops.o syscall.o sched_i386.o sched_pm.o process.o wait.o sched_decay.o libc_wrappers.o \
            pipe.o ansi_handler.o psf.o font_8x16.o

# Unit tests
UNIT_SRCS = unit/drivers/console/test_ansi.c unit/drivers/video/test_psf.c unit/vm/test_kmem.c unit/vm/test_zone.c unit/vm/test_map.c \
             unit/vm/test_object.c unit/vm/test_page.c unit/vm/test_fault.c \
             unit/vm/test_user_memory.c unit/vm/test_swap.c unit/vm/test_swap_real.c unit/vm/test_cow.c \
             unit/arch/test_trampoline.c \
             unit/kern/test_spinlock.c unit/kern/test_kthread.c unit/kern/test_sched.c unit/kern/test_sched_decay.c \
             unit/kern/test_sync.c unit/kern/test_sleepq.c unit/kern/test_signal.c unit/kern/test_pthread_exit.c \
             unit/kern/test_reboot.c \
             unit/vfs/test_fd.c unit/vfs/test_permissions.c unit/vfs/test_chroot.c unit/vfs/test_readdir.c \
             unit/vfs/test_link.c unit/vfs/test_rename.c unit/vfs/test_symlink.c unit/vfs/test_readlink.c \
             unit/vfs/test_namei.c \
             unit/vfs/test_mountopt.c ../sys/vfs/mountopt.c \
             unit/vfs/test_vnode_reclaim.c \
             unit/vfs/test_vnode_init.c \
             unit/vfs/test_vnode_create.c \
             unit/vfs/test_vnode_cache.c \
             unit/vfs/test_vclean.c \
             unit/vfs/test_vnode_lifecycle.c \
             unit/vfs/test_namecache.c \
             unit/fs/test_fuse.c unit/fs/test_ext2_blocks_extent.c \
             unit/kern/prop_sched.c fuzz/fuzz_sched.c \
             unit/perso/test_att.c ../sys/exec/perso/perso_svr3.c ../sys/exec/perso/perso_svr4.c \
             sys/test_sysinfo.c lib/c/test_libc_string_unit.c unit/lib/test_time.c \
             sys/test_div64.c ../tests/sys/test_pipe_race.c sys/test_mmap_batch.c unit/fs/test_procfs.c

UNIT_OBJS = $(notdir $(UNIT_SRCS:.c=.o))

MOCKS_OBJ = mocks.o

all: test_bc_num test_runner test_getuid test_minix test_ext2_perf test_ext2_read_blocks test_ext2_find_next_zero_bit test_ext2_inode_write test_libc_string test_libc_errno test_libc_libgen test_libc_time test_libc_stdlib test_libc_ctype test_libc_assert test_libc_sysctl test_ext2_remove_perf bench_ext2_readdir test_ext2_write_block test_ext2_vfs_ops test_ext2_rename test_libpthread_mutex test_pthread_join test_readline test_history test_bc test_libedit host_test_wait_logic host_test_syscall_abi host_test_libsys_syscall_audit host_test_libsys_error_contract host_test_getopt host_test_pgrp host_test_proc_tree host_test_proc_exit host_test_tty_jobctl host_test_sync_guards host_test_sleepq_private host_test_swapper host_test_rusage host_test_proc_lifecycle host_test_i386_signal host_test_linux_signal host_test_signal_restart host_test_signal_integration host_test_proc_signal_state host_test_rwlock host_test_fork_exec_wait host_test_bio host_test_usb_msc_cbw host_test_lockmgr host_test_isapnp host_test_isa host_test_uart test_getpagesize test_libc_stdio test_libc_printf test_libc_scanf test_libc_prop_stdio test_libc_fuzz_printf test_libc_fuzz_scanf
	./test_runner
	./test_minix
	./test_ext2_find_next_zero_bit
	./test_ext2_perf
	./test_ext2_read_blocks
	./test_ext2_find_next_zero_bit
	./test_ext2_inode_write
	./test_libc_string
	./test_libc_errno
	./test_libc_libgen
	./test_libc_time
	./test_libc_stdlib
	./test_libc_ctype
	./test_libc_assert
	./test_libc_stdio
	./test_libc_printf
	./test_libc_scanf
	./test_libc_prop_stdio
	./test_libc_fuzz_printf
	./test_libc_fuzz_scanf
	./test_ext2_remove_perf
	./bench_ext2_readdir
	./test_ext2_write_block
	./test_ext2_vfs_ops
	./test_ext2_rename
	./test_libpthread_mutex
	./test_pthread_join
	$(MAKE) -C lib/edit
	./lib/edit/host_test_history
	./test_readline
	./test_libedit
	./test_bc
	./test_arith
	./test_bc_num
	./host_test_wait_logic
	./host_test_syscall_abi
	@$(MAKE) host_test_libsys_syscall_audit
	./host_test_libsys_error_contract
	./host_test_getopt
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
	./host_test_bio
	./host_test_usb_msc_cbw
	./host_test_lockmgr
	./host_test_isapnp
	./host_test_isa
	./host_test_uart
	./test_getpagesize
	./test_getuid
	./test_getrusage
	./host_test_bm2

test_getpagesize: lib/sys/test_getpagesize.c ../lib/sys/getpagesize.c
	$(CC) $(CFLAGS_COMMON) $^ -o $@

test_getuid: lib/sys/test_getuid.c
# -I../lib/sys: the test #includes ../../../lib/sys/getuid.c, which pulls in
# <sysret.h> -- a libsys-private header that only exists in that directory.
	$(CC) $(CFLAGS_COMMON) -D__i386__ -I../sys -I../include -I../sys/include -I../lib/sys $^ -o $@

test_getrusage: lib/sys/test_getrusage.c
	$(CC) $(CFLAGS_COMMON) -D__i386__ -I../sys -I../sys/arch/i386 -I../include $^ -o $@

EDIT_SRCS_NO_RL = ../lib/edit/editline.c ../lib/edit/terminal.c \
                  ../lib/edit/history.c ../lib/edit/signal.c ../lib/edit/keymap.c \
                  ../lib/edit/tokenizer.c ../lib/edit/rl_compat.c ../lib/edit/utf8.c

test_readline: lib/edit/test_readline.c $(EDIT_SRCS_NO_RL)
	$(CC) $(CFLAGS_COMMON) -Ilib/edit -I../sys -I../lib/edit $< $(EDIT_SRCS_NO_RL) -Wl,--allow-multiple-definition -o $@

EDIT_SRCS = ../lib/edit/editline.c ../lib/edit/readline.c ../lib/edit/terminal.c \
            ../lib/edit/history.c ../lib/edit/signal.c ../lib/edit/keymap.c \
            ../lib/edit/tokenizer.c ../lib/edit/rl_compat.c ../lib/edit/utf8.c

test_libedit: lib/edit/test_editline.c $(EDIT_SRCS)
	$(CC) $(CFLAGS_COMMON) -Ilib/edit -I../sys -I../lib/edit $< $(EDIT_SRCS) -o $@

host_test_bm2: sys/host_test_bm2.c
	$(CC) $(CFLAGS_COMMON) $< -o $@

host_test_wait_logic: sys/host_test_wait_logic.c
	$(CC) $(CFLAGS_KERN) $< -o $@

# ABI conformance test for lib/sys/syscall.S — must be built -m32
# so the i386 stub object can be linked in and the int $0x80 site
# patched at runtime.
host_test_syscall_abi: lib/sys/host_test_syscall_abi.c ../lib/sys/syscall.S
	$(CC) -m32 -Wall -O0 -g -D_GNU_SOURCE $^ -o $@

# Static audit: every SYS_* used in lib/sys must be defined in
# sys/arch/i386/syscall.h, and no raw integer may be passed as the
# syscall number to syscall(3).  Implemented as a shell script so
# we can run it from the Makefile without a build step.
.PHONY: host_test_libsys_syscall_audit
host_test_libsys_syscall_audit: lib/sys/host_test_libsys_syscall_audit.sh
	@bash $<

# Error contract test: verify the negated-errno-to-errno+(-1)
# normalization documented in docs/syscalls/error-contract.md.
host_test_libsys_error_contract: lib/sys/host_test_libsys_error_contract.c
	$(CC) $(CFLAGS_COMMON) $< -o $@

# getopt(3) / getopt_long(3) / getopt_long_only(3) coverage —
# host-builds lib/c/src/getopt.c via #include and exercises every
# documented call shape.
host_test_getopt: lib/c/host_test_getopt.c ../lib/c/src/getopt.c
	$(CC) -Wall -O0 -g -m32 \
	    -I../include -I../sys/include -I../sys \
	    $< -o $@

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

host_test_bio: sys/host_test_bio.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_usb_msc_cbw: sys/host_test_usb_msc_cbw.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_lockmgr: sys/host_test_lockmgr.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_isapnp: sys/host_test_isapnp.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_isa: sys/host_test_isa.c
	$(CC) $(CFLAGS_KERN) $< -o $@

host_test_uart: sys/host_test_uart.c
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

test_pthread_join: lib/test_pthread_join.c ../lib/pthread/pthread_create.c
# -D__i386__ so <sys/syscall.h> picks the real syscall table rather than the
# x86_64 stub; -I../lib/pthread for the private pthread_internal.h.
	$(CC) $(CFLAGS_COMMON) -Wno-int-conversion -Wno-pointer-to-int-cast -D__i386__ -I../include -I../sys/include -I../sys -I../lib/pthread $< -o $@

test_libpthread_mutex: lib/test_pthread_mutex.c ../lib/pthread/pthread_mutex.c
	$(CC) $(CFLAGS_COMMON) -D__i386__ -I../include -I../sys/include -I../sys -I../lib/pthread $< -o $@

test_history:
	$(MAKE) -C lib/edit

test_libc_assert: lib/c/test_assert.c ../lib/c/src/assert.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin lib/c/test_assert.c -o $@

test_libc_sysctl: lib/c/test_sysctl.c
# -I../lib/c/include: sysctl_helpers.c includes <sys_local.h>, a libc-private
# header that lives only there.
	$(CC) $(CFLAGS_COMMON) -fno-builtin -I../include -I../lib/c/include lib/c/test_sysctl.c -o $@

test_libc_ctype: lib/c/test_ctype.c ../lib/c/src/ctype.c
# ctype.c defines the _ISalnum/_ISblank enumerators itself; against glibc's
# <ctype.h> those are redeclarations.  Point the include path at substrate's
# own headers, which is whose ctype this is.
	$(CC) $(CFLAGS_COMMON) -fno-builtin -D__i386__ -I../include -I../sys -I../sys/include lib/c/test_ctype.c -o $@

test_ext2_perf: unit/fs/test_ext2_perf_host.c
	$(CC) $(CFLAGS_KERN) $< unit/fs/ext2_host_stubs.c -o $@

test_ext2_read_blocks: unit/fs/test_ext2_read_blocks_host.c
	$(CC) $(CFLAGS_KERN) $< unit/fs/ext2_host_stubs.c -o $@

test_ext2_find_next_zero_bit: unit/fs/test_ext2_find_next_zero_bit.c
	$(CC) $(CFLAGS_KERN) $< unit/fs/ext2_host_stubs.c -o $@

test_ext2_inode_write: unit/fs/test_ext2_inode_write_host.c
	$(CC) $(CFLAGS_KERN) $< unit/fs/ext2_host_stubs.c -o $@

test_ext2_vfs_ops: unit/fs/test_ext2_vfs_ops_host.c
	$(CC) $(CFLAGS_KERN) $< unit/fs/ext2_host_stubs.c -o $@

test_ext2_rename: unit/fs/test_ext2_rename_host.c
	$(CC) $(CFLAGS_KERN) $< unit/fs/ext2_host_stubs.c -o $@

bench_ext2_readdir: unit/fs/bench_ext2_readdir_host.c
	$(CC) $(CFLAGS_KERN) $< unit/fs/ext2_host_stubs.c -o $@

test_ext2_remove_perf: unit/fs/test_ext2_remove_perf.c
	$(CC) $(CFLAGS_KERN) $< unit/fs/ext2_host_stubs.c -o $@

test_minix: unit/fs/test_minix_standalone.c
	$(CC) $(CFLAGS_KERN) $< -o $@

test_libc_string: lib/c/test_libc_string_unit.c ../lib/c/src/string.c string_prefixed.o libc_wrappers.o
	$(CC) $(CFLAGS_LIBC_HOST) -fno-builtin -c ../lib/c/src/string.c -o string_raw.o
	objcopy --prefix-symbols=libc_ string_raw.o string_prefixed.o
	$(CC) $(CFLAGS_COMMON) lib/c/test_libc_string_unit.c string_prefixed.o libc_wrappers.o -o $@

test_libc_errno: lib/c/test_errno.c ../lib/c/src/sys.c
	$(CC) $(CFLAGS_COMMON) -D__i386__ -fno-builtin -I../lib/c/include -I../include -I../sys -I../sys/include lib/c/test_errno.c ../lib/c/src/sys.c -o $@

test_libc_libgen: lib/c/test_libgen.c ../lib/c/src/libgen.c string_prefixed.o libc_wrappers.o
	$(CC) $(CFLAGS_LIBC_HOST) -fno-builtin -c ../lib/c/src/libgen.c -o libgen_raw.o
	objcopy --prefix-symbols=libc_ libgen_raw.o libgen_prefixed.o
	$(CC) $(CFLAGS_COMMON) lib/c/test_libgen.c libgen_prefixed.o string_prefixed.o libc_wrappers.o -o $@

test_libc_time: lib/c/test_time_standalone.c ../lib/c/src/time/time.c
	$(CC) $(CFLAGS_COMMON) -fno-builtin lib/c/test_time_standalone.c -o $@

test_libc_stdlib: lib/c/test_stdlib.c ../lib/c/src/stdlib.c
# stdlib.c includes <sys/syscall.h> for SYS_SETSID / SYS_GETRANDOM (daemon(),
# login_tty(), the entropy path).  Without substrate's include tree that is
# glibc's header, which spells them SYS_setsid; with it, the header
# dispatches on __i386__ vs __x86_64__ and a 64-bit host build lands in the
# barely-scaffolded x86_64 table.  -D__i386__ picks the real one, which is
# exactly what test_libc_errno does for the same reason.
	$(CC) $(CFLAGS_COMMON) -fno-builtin -D__i386__ -I../include -I../sys -I../sys/include lib/c/test_stdlib.c -o $@

libc_stdio_prefixed.o: ../lib/c/stdio/stdio_core.c ../lib/c/stdio/stdstreams.c ../lib/c/stdio/printf.c ../lib/c/stdio/scanf.c ../lib/c/src/stdlib.c ../lib/c/src/string.c ../lib/c/src/ctype.c symbols.map
	$(CC) $(CFLAGS_COMMON) -m32 -fno-builtin -I../lib/c/include -I../include -I../sys -c ../lib/c/stdio/stdio_core.c -o stdio_core_raw.o
	$(CC) $(CFLAGS_COMMON) -m32 -fno-builtin -I../lib/c/include -I../include -I../sys -c ../lib/c/stdio/stdstreams.c -o stdstreams_raw.o
	$(CC) $(CFLAGS_COMMON) -m32 -fno-builtin -I../lib/c/include -I../include -I../sys -c ../lib/c/stdio/printf.c -o printf_raw.o
	$(CC) $(CFLAGS_COMMON) -m32 -fno-builtin -I../lib/c/include -I../include -I../sys -c ../lib/c/stdio/scanf.c -o scanf_raw.o
	$(CC) $(CFLAGS_COMMON) -m32 -fno-builtin -I../lib/c/include -I../include -I../sys -c ../lib/c/src/stdlib.c -o stdlib_raw.o
	$(CC) $(CFLAGS_COMMON) -m32 -fno-builtin -I../lib/c/include -I../include -I../sys -c ../lib/c/src/string.c -o string_raw.o
	$(CC) $(CFLAGS_COMMON) -m32 -fno-builtin -I../lib/c/include -I../include -I../sys -c ../lib/c/src/ctype.c -o ctype_raw.o
	$(LD) -m elf_i386 -r stdio_core_raw.o stdstreams_raw.o printf_raw.o scanf_raw.o stdlib_raw.o string_raw.o ctype_raw.o -o stdio_combined_raw.o
	objcopy --redefine-syms=symbols.map stdio_combined_raw.o $@

test_libc_stdio: lib/c/unit/test_stdio.c libc_stdio_prefixed.o
	$(CC) $(CFLAGS_COMMON) -m32 $^ -o $@

test_libc_printf: lib/c/unit/test_printf.c libc_stdio_prefixed.o
	$(CC) $(CFLAGS_COMMON) -m32 $^ -o $@

test_libc_scanf: lib/c/unit/test_scanf.c libc_stdio_prefixed.o
	$(CC) $(CFLAGS_COMMON) -m32 $^ -o $@

test_libc_prop_stdio: lib/c/property/prop_stdio.c libc_stdio_prefixed.o
	$(CC) $(CFLAGS_COMMON) -m32 $^ -o $@

test_libc_fuzz_scanf: lib/c/fuzz/fuzz_scanf.c libc_stdio_prefixed.o
	$(CC) $(CFLAGS_COMMON) -m32 $^ -o $@

test_libc_fuzz_printf: lib/c/fuzz/fuzz_printf.c libc_stdio_prefixed.o
	$(CC) $(CFLAGS_COMMON) -m32 $^ -o $@

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

%.o: ../sys/drivers/video/%.c
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
%.o: unit/drivers/video/%.c
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

test_ext2_blocks_extent.o: unit/fs/test_ext2_blocks_extent.c
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
	$(CC) $(CFLAGS_LIBC_HOST) -fno-builtin -c ../lib/c/src/string.c -o string_raw.o
	objcopy --prefix-symbols=libc_ string_raw.o string_prefixed.o

clean:
	rm -f *.o test_runner test_minix test_ext2_perf test_ext2_read_blocks test_ext2_find_next_zero_bit test_ext2_inode_write test_libc_string test_libc_errno test_libc_libgen test_libc_time test_libc_stdlib test_libc_ctype test_libc_assert test_ext2_remove_perf bench_ext2_readdir test_ext2_write_block test_ext2_vfs_ops test_ext2_rename test_libpthread_mutex test_pthread_join test_readline test_libedit test_bc host_test_wait_logic host_test_syscall_abi host_test_libsys_error_contract host_test_getopt host_test_pgrp host_test_proc_tree host_test_proc_exit host_test_tty_jobctl host_test_sync_guards host_test_sleepq_private host_test_swapper host_test_rusage host_test_proc_lifecycle host_test_i386_signal host_test_signal_restart host_test_signal_integration host_test_rwlock host_test_fork_exec_wait host_test_bio host_test_usb_msc_cbw host_test_lockmgr host_test_isapnp host_test_isa host_test_uart test_getpagesize test_getuid test_bm
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
	$(CC) $(CFLAGS_COMMON) -I../usr.lib/bc $< ../usr.lib/bc/num.c ../usr.lib/bc/arith.c -o $@

test_arith: usr.lib/bc/test_arith.c ../usr.lib/bc/num.c ../usr.lib/bc/arith.c
	$(CC) $(CFLAGS_COMMON) -I../usr.lib/bc $< ../usr.lib/bc/num.c ../usr.lib/bc/arith.c -o $@

test_ext2_write_block: unit/fs/test_ext2_write_block_host.c
	$(CC) $(CFLAGS_KERN) $< unit/fs/ext2_host_stubs.c -o $@

test_bm: test_bm.c
	$(CC) $(CFLAGS_COMMON) $< -o $@

test_bc_num: usr.lib/bc/test_num.c ../usr.lib/bc/arith.c
	$(CC) $(CFLAGS_COMMON) -I../usr.lib/bc $< ../usr.lib/bc/arith.c -o $@
