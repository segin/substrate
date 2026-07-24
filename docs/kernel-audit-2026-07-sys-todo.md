# Kernel sys/ audit — remediation checklist (2026-07-22)

Source: `docs/kernel-audit-2026-07-sys.md`. Work one checkbox at a time: fix → build → commit. 87 items.


## CRITICAL

- [x] **A01** [sys/arch/i386/pmap.c:1460] ptrace PEEK/POKE reaches kernel VAs — arbitrary kernel read/write
- [x] **A02** [sys/drivers/console/pty.c:747] PTY master close frees pair while slave still open (UAF)
- [x] **A03** [sys/kern/sleepq.c:332] Single-thread wakeups swallowed by STOPPED/ZOMBIE head waiter
- [x] **A04** [sys/pm/wait.c:158] kern_wait4 walks and reaps children with no locking — double-reap double-free

## HIGH

- [x] **A05** [sys/arch/i386/pmap.c:1897] TLB shootdown ACK counter is global and shared across rounds; wait times out silently
- [x] **A06** [sys/drivers/usb/uac.c:684] uac_detach: use-after-free of freed usb_device via feeder kthread on hot-unplug
- [x] **A07** [sys/drivers/video/fb.c:593] Runtime FBIOPUT_VIDEO_MODE desyncs fb geometry from console shadow buffer -> OOB shadow read
- [x] **A08** [sys/exec/formats/elf.c:1700] exec raises setuid/setgid credentials and resets signal dispositions before the last fallible step
- [x] **A09** [sys/exec/formats/elf.c:756] Double-free of segment pages on elf_load error paths
- [x] **A10** [sys/exec/formats/elf.c:1743] Failed execve after setuid does not roll back credentials or signal state
- [x] **A11** [sys/exec/perso/perso_freebsd.c:106] FreeBSD gettimeofday overruns caller's 8-byte struct timeval
- [x] **A12** [sys/exec/perso/perso_openbsd.c:59] OpenBSD getrusage writes to unvalidated user pointer
- [x] **A13** [sys/fs/ext2/ext2.c:680] Unbounded eh_ecount in ext4 extent resolver → OOB read
- [x] **A14** [sys/fs/ext2/ext2.c:2213] blocks_per_group / inodes_per_group not bounded to block_size*8 → OOB bitmap scan
- [x] **A15** [sys/fs/fat/fat.c:490] FAT root node not pinned in node cache — recycled after 64 lookups
- [x] **A16** [sys/fs/fat/fat.c:946] Cluster allocator is not atomic — concurrent allocation cross-links files
- [x] **A17** [sys/fs/fat/fat.c:1086] FAT write/directory paths share function-static scratch buffers with no locking
- [x] **A18** [sys/fs/fat/fat.c:1367] FAT directory-entry write assumes physical contiguity across a cluster boundary
- [x] **A19** [sys/fs/shmfs.c:196] shmfs_write: size_t overflow of (off+sz) defeats grow → OOB write
- [x] **A20** [sys/kern/geom/geom_gpt.c:148] GPT entry array parsed with unbounded on-disk entry_size -> OOB read
- [x] **A21** [sys/kern/geom/geom_gpt.c:134] GPT entries_per_sector division by zero when entry_size > 512
- [x] **A22** [sys/kern/mutex.c:146] proc_exit force-releases mutexes of siblings still running on other CPUs
- [x] **A23** [sys/net/af_unix.c:1319] send()/sendto()/sendmsg() memcpy raw user data buffer in kernel context (no copyin/bounce)
- [x] **A24** [sys/net/af_unix.c:964] sys_socketpair writes result fds directly to user sv[] pointer without copyout
- [x] **A25** [sys/pm/pgrp.c:213] pgrp_remove_proc reads and frees old_pgrp after dropping proctree_lock
- [x] **A26** [sys/pm/process.c:1792] proc_exit publishes reapable state while threads may still run on other CPUs
- [x] **A27** [sys/pm/wait.c:160] Concurrent wait4() double-frees a zombie child
- [x] **A28** [sys/vfs/vfs.c:1371] mountlist traversed lock-free while mount/unmount mutate and kfree() entries
- [x] **A29** [sys/vm/uma_core.c:998] UMA page hash walked without slab lock — use-after-free of freed slab headers
- [~] **A30** [sys/vm/vm_fault.c:160] Write to PROT_READ / PROT_NONE private mapping silently granted (protection bypass) (REVERTED — broke ld.so relocation of RO segments)
- [x] **A31** [sys/vm/vm_object.c:172] In-place COW eviction frees a still-mapped frame; later pmap_enter unholds a recycled frame

## MEDIUM

- [x] **A32** [sys/arch/i386/fpu/fpu_emu.c:20] Lazy-FPU owner is a single global, not per-CPU
- [x] **A33** [sys/drivers/usb/usb.c:840] Hub-downstream devices are never disconnected or quiesced on hot-unplug
- [x] **A34** [sys/drivers/usb/xhci.c:281] xHCI slot, device/input contexts and transfer rings leaked on every device disconnect
- [x] **A35** [sys/drivers/virtio/virtio_blk.c:155] virtio-blk synchronous read races shared descriptor ring on concurrent callers
- [x] **A36** [sys/exec/formats/elf.c:283] Unsynchronized global ELF image cache — torn reads under preemption
- [x] **A37** [sys/exec/perso/perso_freebsd.c:107] FreeBSD getrusage/getitimer/wait4 native handlers overrun 8-byte-timeval structs
- [x] **A38** [sys/fs/ext2/ext2.c:2683] ext2_free_inode mutates node cache without ext2_node_cache_lock
- [x] **A39** [sys/fs/udf/udf_write.c:782] UDF udf_add_fid: only one sector read but directory treated as 4096 bytes
- [x] **A40** [sys/kern/geom/geom_mbr.c:63] MBR extended-partition (EBR) chain parse can loop forever on crafted media
- [x] **A41** [sys/kern/lockmgr.c:143] Single LK_WANT_EXCL bit clobbered across concurrent exclusive waiters
- [x] **A42** [sys/kern/rwlock.c:89] rwlock waiting_writers leaks permanently when a parked writer is killed
- [x] **A43** [sys/kern/turnstile.c:131] Turnstile pool exhaustion panics the kernel (128 contended locks)
- [x] **A44** [sys/net/af_unix.c:602] AF_UNIX SOCK_DGRAM datagram > 65535 bytes truncates its 2-byte length header, corrupting frame boundaries
- [x] **A45** [sys/net/tcp.c:1176] Use-after-free of child PCBs in tcp_close() LISTEN teardown
- [x] **A46** [sys/pm/pgrp.c:278] pgrp/session syscalls return bare -1 instead of -errno
- [x] **A47** [sys/pm/process.c:480] fork failure returns bare -1 → userspace sees EPERM instead of ENOMEM/EAGAIN
- [x] **A48** [sys/pm/process.c:563] Non-atomic f_count++ during fork races sibling-thread close
- [x] **A49** [sys/pm/sched.c:960] sched_get_thread returns unreferenced pointer; lwp ops write to possibly-freed thread_t
- [x] **A50** [sys/vfs/vfs.c:954] Legacy fs op-dispatch helpers return bare -1, surfacing to userspace as EPERM
- [x] **A51** [sys/vm/vm_kmem.c:200] kfree trusts caller-supplied size; cross-zone free reaches the per-CPU bucket unvalidated
- [x] **A52** [sys/vm/vm_page.c:606] vm_page_free on an already-freed page clears PG_FREE, defeating the buddy double-free guard
- [x] **A53** [sys/vm/vm_page.c:872] Pagedaemon and stats paths iterate page queues without the queue lock
- [x] **A54** [sys/kern/lockmgr.c:118] lockmgr LK_WANT_EXCL stuck set when a parked exclusive waiter is killed

## LOW

- [x] **A55** [sys/arch/i386/pmap.c:664] pmap_fork treats a large-page (PTE_PS) user PDE as a page table
- [x] **A56** [sys/arch/i386/signal.c:405] Signal handler entered with 16-byte-aligned ESP (i386 SysV ABI wants ESP%16==12)
- [x] **A57** [sys/arch/i386/syscall.c:606] arch_fork_with_stack / arch_clone_thread return bare -1 (wrong errno contract)
- [x] **A58** [sys/drivers/video/psf.c:80] psf2_parse: unsigned underflow of (size - headersize) bypasses glyph-data bounds check
- [x] **A59** [sys/exec/formats/elf.c:1531] execve returns bare -1 (maps to EPERM) instead of correct errno
- [x] **A60** [sys/exec/perso/compat.c:244] sys_mprotect end computation can wrap 32-bit, silently protecting nothing
- [x] **A61** [sys/exec/perso/perso_netbsd.c:34] getrusage wrappers return bare -1 instead of -errno (committed with ksem work)
- [x] **A62** [sys/exec/perso/perso_svr3.c:46] SVR3/SVR4: ulimit(2) dispatched to sys_dup2
- [x] **A63** [sys/fs/ext2/ext2.c:3235] rename '..' fixup trusts dot->rec_len → OOB access in dir block buffer
- [x] **A64** [sys/fs/minix/minix.c:905] minix_finddir: kmalloc result used without NULL check
- [x] **A65** [sys/fs/udf/udf_write.c:1181] UDF error paths leak kmalloc'd buffers
- [x] **A66** [sys/kern/ipc_shm.c:280] shmat with explicit address destroys existing mapping before it can fail
- [x] **A67** [sys/kern/runqueue.c:56] Timeshare MLFQ level uses bitwise AND (& 39) instead of range clamp
- [x] **A68** [sys/kern/sched_ipi.c:26] Preemption IPI assumes CPU index == LAPIC ID
- [x] **A69** [sys/kern/turnstile.c:196] turnstile_release clobbers priority boosts from other still-held locks
- [x] **A70** [sys/kern/turnstile.c:43] turnstile global lock taken without preempt_disable
- [x] **A71** [sys/net/tcp.c:488] SYN_SENT accepts SYN|ACK without validating the acknowledgement number
- [x] **A72** [sys/pm/pgrp.c:342] pgrp/session syscalls return bare -1 instead of -errno (already fixed by A46)
- [x] **A73** [sys/pm/pgrp.c:397] sys_setpgid leaks a freshly allocated session when pgrp_alloc fails
- [x] **A74** [sys/pm/process.c:627] fork error paths and proc_destroy leak the chroot root_node vnode reference
- [x] **A75** [sys/pm/sched.c:413] sched_yield walks allthread without tid_lock (KERN-06 gap) — SMP UAF
- [x] **A76** [sys/pm/wait.c:192] Grandchildren's fault/context-switch counters are double-counted in wait4
- [x] **A77** [sys/vm/phys_mem.c:513] vm_phys_mark_used reserves pages without setting PG_PMM_ALLOC
- [x] **A78** [sys/vm/uma_core.c:1000] uma_zfree double-free guard is a non-atomic check-then-set (TOCTOU) (resolved by A29 slab-lock)
- [x] **A79** [sys/vm/vm_kmem.c:300] kmem_get_stats/kmem_get_snapshot take kmem_stats_lock without disabling IRQs
- [x] **A80** [sys/vm/vm_object.c:16] vm_object bootstrap pool allocation counter is unsynchronized
- [x] **A81** [sys/vm/vm_syscalls.c:694] sys_msync returns bare -1 instead of -errno
- [x] **A82** [sys/arch/i386/pmap.c:838] pmap_enter on a non-current pmap switches CR3 but is not preemption-safe
- [x] **A83** [sys/drivers/video/bdf_pcf.c:298] bdf_parse: 32-bit multiply overflow in total_glyph_size -> undersized kmalloc -> heap overflow
- [x] **A84** [sys/drivers/video/bdf_pcf.c:91] pcf_parse: uint32 overflow of offset+constant bypasses table bounds checks -> wild-pointer OOB read
- [x] **A85** [sys/fs/udf/udf_write.c:692] UDF inline write: ext_attr_length underflow + offset+size overflow → heap OOB write
- [x] **A86** [sys/kern/runqueue.c:110] runqueue_remove recomputes level from mutable priority -> wrong-queue list corruption
- [x] **A87** [sys/pm/sched.c:415] sched_yield pick loop walks allthread without tid_lock (SMP UAF)
