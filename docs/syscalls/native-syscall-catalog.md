# Native Syscall Catalog (`substrate` personality)

Source of truth:
- Numbers: `sys/arch/i386/syscall.h`
- Native dispatch wiring: `sys/exec/perso/perso_native.c`

## Wired In Native Dispatch (82 entries)

### Process, identity, scheduler

- `1 SYS_EXIT (exit)`: terminate current process/thread.
- `2 SYS_FORK (fork)`: create child process.
- `11 SYS_EXECVE (execve)`: execute program image.
- `20 SYS_GETPID (getpid)`: get current PID.
- `64 SYS_GETPPID (getppid)`: get parent PID.
- `23 SYS_SETUID (setuid)`: set effective UID with privilege checks.
- `24 SYS_GETUID (getuid)`: get real UID.
- `46 SYS_SETGID (setgid)`: set effective GID with privilege checks.
- `47 SYS_GETGID (getgid)`: get real GID.
- `49 SYS_GETEUID (geteuid)`: get effective UID.
- `50 SYS_GETEGID (getegid)`: get effective GID.
- `147 SYS_SETSID (setsid)`: create a new session.
- `310 SYS_GETSID (getsid)`: get session ID for PID.
- `181 SYS_SETPGID (setpgid)`: set process group.
- `182 SYS_GETPGID (getpgid)`: get process group.
- `96 SYS_SETPRIORITY (setpriority)`: set nice value.
- `100 SYS_GETPRIORITY (getpriority)`: query nice value.
- `117 SYS_GETRUSAGE (getrusage)`: return resource usage struct.

### File, VFS, and I/O

- `3 SYS_READ (read)`: read from file descriptor.
- `4 SYS_WRITE (write)`: write to file descriptor.
- `5 SYS_OPEN (open)`: open file path.
- `6 SYS_CLOSE (close)`: close file descriptor.
- `10 SYS_UNLINK (unlink)`: remove directory entry.
- `33 SYS_ACCESS (access)`: access checks.
- `39 SYS_MKDIR (mkdir)`: create directory.
- `40 SYS_RMDIR (rmdir)`: remove directory.
- `42 SYS_PIPE (pipe)`: create pipe pair.
- `54 SYS_IOCTL (ioctl)`: device-specific control.
- `63 SYS_DUP2 (dup2)`: duplicate FD to target FD.
- `85 SYS_READLINK (readlink)`: read symlink target.
- `106 SYS_STAT (stat)`: file metadata by path.
- `107 SYS_LSTAT (lstat)`: symlink metadata.
- `108 SYS_FSTAT (fstat)`: file metadata by FD.
- `141 SYS_GETDENTS (getdents)`: read directory entries.
- `183 SYS_GETCWD (getcwd)`: get working directory path.
- `21 SYS_MOUNT (mount)`: mount filesystem.
- `22 SYS_UMOUNT (umount)`: unmount target.
- `36 SYS_SYNC (sync)`: flush filesystem state.
- `51 SYS_ACCT (acct)`: process accounting control.
- `19 SYS_LSEEK (lseek)`: 64-bit file offset seek (split hi/lo args).

### Time and clocks

- `13 SYS_TIME (time)`: current epoch seconds.
- `25 SYS_STIME (stime)`: set system time.
- `43 SYS_TIMES (times)`: process accounting ticks.
- `162 SYS_NANOSLEEP (nanosleep)`: sleep with nanosecond granularity.

### Signals and context

- `48 SYS_SIGNAL (signal)`: legacy signal handler set.
- `67 SYS_SIGACTION (sigaction)`: POSIX signal action interface.
- `72 SYS_SIGSUSPEND (sigsuspend)`: suspend until signal delivery.
- `73 SYS_SIGPENDING (sigpending)`: fetch pending signal mask.
- `126 SYS_SIGPROCMASK (sigprocmask)`: update signal mask.
- `186 SYS_SIGALTSTACK (sigaltstack)`: alternate signal stack.
- `119 SYS_SIGRETURN (sigreturn)`: return from signal frame.
- `247 SYS_RT_SIGRETURN (rt_sigreturn)`: realtime signal return path.
- `37 SYS_KILL (kill)`: send signal to PID or process group.

### Threads and low-level arch

- `431 SYS_THR_EXIT (thr_exit)`: terminate current thread.
- `432 SYS_THR_SELF (thr_self)`: get TID.
- `455 SYS_THR_NEW (thr_new)`: create thread.
- `457 SYS_THR_JOIN (thr_join)`: join thread.
- `123 SYS_MODIFY_LDT (modify_ldt)`: LDT/TLS descriptor update.

### Memory and VM

- `90 SYS_MMAP (mmap)`: map memory/file region.
- `144 SYS_MSYNC (msync)`: sync mapped pages.
- `150 SYS_mlock (mlock)`: currently no-op stub (returns success).
- `151 SYS_munlock (munlock)`: currently no-op stub (returns success).
- `241 SYS_PMAP_STATS (pmap_stats)`: kernel pmap counters.
- `255 SYS_VM_STATS (vm_stats)`: VM summary statistics.

### System information and control

- `88 SYS_REBOOT (reboot)`: reboot/halt/power-off control.
- `116 SYS_SYSINFO (sysinfo)`: Linux-style system info struct.
- `122 SYS_UNAME (uname)`: kernel identity info.
- `202 SYS_SYSCTL (sysctl)`: MIB-based kernel tunables.
- `209 SYS_POLL (poll)`: poll descriptors for readiness.

### Process introspection (`sys_proc_*`)

- `242 SYS_PROC_INFO (proc_info)`: single-process info struct.
- `243 SYS_PROC_LIST (proc_list)`: enumerate active PIDs.
- `244 SYS_PROC_COUNT (proc_count)`: count active processes.
- `248 SYS_PROC_THREADS (proc_threads)`: currently stub.
- `249 SYS_PROC_FDS (proc_fds)`: currently stub.
- `250 SYS_PROC_MAPS (proc_maps)`: currently stub.
- `251 SYS_PROC_CWD (proc_cwd)`: currently stub.
- `252 SYS_PROC_EXE (proc_exe)`: currently stub.
- `253 SYS_PROC_CMDLINE (proc_cmdline)`: currently stub.
- `254 SYS_PROC_ENVIRON (proc_environ)`: currently stub.
- `245 SYS_CPU_COUNT (cpu_count)`: online CPU count.
- `246 SYS_HOSTNAME (hostname)`: kernel hostname copyout.

### Explicit compatibility stubs currently wired

- `26 SYS_PTRACE (ptrace)`: wired to compatibility stub (`-ENOSYS`).

## Numbered Constants Defined But Not Wired In Native Dispatch

These constants exist in `sys/arch/i386/syscall.h` but are not currently in the native syscall table.

- `7 SYS_WAITPID`
- `8 SYS_CREAT`
- `9 SYS_LINK`
- `14 SYS_MKNOD`
- `15 SYS_CHMOD`
- `16 SYS_LCHOWN`
- `38 SYS_RENAME`
- `41 SYS_DUP`
- `45 SYS_BRK`
- `60 SYS_UMASK`
- `61 SYS_CHROOT`
- `91 SYS_MUNMAP`
- `92 SYS_TRUNCATE`
- `93 SYS_FTRUNCATE`
- `120 SYS_CLONE`
- `240 SYS_FUTEX`
- `265 SYS_CLOCK_GETTIME`

## Additional Wrapper-Only Numbers (Not in header or native map)

- `SYS_vm86 = 113` in `lib/sys/vm86.c`
- `SYS_PROC_PERS_NAME = 360` in `lib/sys/proc.c`
