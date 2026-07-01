# Substrate — Open POSIX Test Suite conformance baseline

First on-substrate OPTS conformance run.  This baseline is **step 1 toward
passing the suite**: every FAIL / TIMEOUT / CRASH / kernel-PANIC below is a
substrate conformance gap and a roadmap item.

> **Run 2 (kernel `07ecdb34`) is at the bottom of this file** — a
> re-measurement after the clock / mqueue / SCHED / signals-RT / pthread-timed
> rounds merged.  Everything from here through "Notable clusters / observations"
> is **Run 1** (kernel `5100a8e6`); the Run 2 section supersedes it.

- Suite: Open POSIX Test Suite (from LTP, commit `01d0eecd`), conformance +
  functional tests.
- Kernel: `sys/kernel.multiboot` at `main` = `5100a8e6` (POSIX mqueue + ksem
  named semaphores + userspace librt merged).
- Toolchain: `i386-unknown-substrate-gcc` 16.1.0; tests linked dynamically
  `-lrt -lpthread -lsys -lm` (librt is dynamic: `librt.so.0`).
- Harness: `run-baseline.sh` — fresh `mke2fs -d` test image as a 2nd AHCI
  disk, `opts-driver.sh` runs each binary as PID-1 child under a per-test
  watchdog timeout, maps the OPTS exit code to a verdict, resumes across a
  fresh boot when a test panics the kernel.
- Accelerator: QEMU `-accel kvm`.  Note: a documented QEMU/KVM i386 bug can
  corrupt memory just after a signal handler returns; a TCG spot-check of the
  signals area reproduced the sigaction FAILs, so they are substrate issues
  rather than KVM artifacts, but signal-area numbers are best re-confirmed
  under `ACCEL=tcg`.

## Verdict legend

`PASS`=0 `FAIL`=1 `UNRESOLVED`=2 `UNSUPPORTED`=4 `UNTESTED`=5 (OPTS exit codes);
`TIMEOUT`=watchdog-killed hang; `CRASH`=test process died on a signal;
`PANIC`=killed/wedged the **kernel** (recovered by rebooting past it).

## Per-area result (built = tests that compiled & were run)

```
AREA             BUILT  BFAIL |  PASS  FAIL  UNRE  UNSU  UNTE   TMO  CRSH  PANC    RUN
---                ---    --- |   ---   ---   ---   ---   ---   ---   ---   ---    ---
threads            203    154 |   163    18     1     7     0    13     0     1    203
signals            613     41 |   406   101    98     0     0     3     2     2    612
semaphores          73      1 |    59     5     5     0     1     0     0     3     73
timers              95      7 |    28    16    43     5     1     2     0     0     95
mmap                94      0 |    35    13    31     4     5     1     4     1     94
sched               65      3 |     2    20    24    12     0     0     0     7     66
message_queues     111     10 |   101     8     1     0     0     1     0     0    111
aio                  0     72 |     0     0     0     0     0     0     0     0      0
other               36      6 |    24     6     3     0     0     2     0     1     36
-------------------------------------------------------------------------------------
TOTAL run: PASS=818 FAIL=187 UNRESOLVED=206 UNSUPPORTED=28 UNTESTED=7 TIMEOUT=22 CRASH=6 PANIC=15
```

`BUILT` = compiled + run; `BFAIL` = failed to compile/link (a missing
substrate POSIX symbol — see below).  `RUN` counts include the recovered
per-test rows; two malformed serial rows are excluded from the table.

## Build coverage (1291 of 1585 built-area tests compiled; 294 build-fails)

No area is skipped wholesale — as of `5100a8e6`, `<mqueue.h>`, `<aio.h>`,
`struct sigevent`, named semaphores and `librt` all exist.  Build failures are
**real substrate header/lib gaps**, deliberately not patched around:

- **aio: 0 / 72 build** — every aio + `lio_listio` test needs
  `_SC_ASYNCHRONOUS_IO`, which substrate `<unistd.h>` does not define (and
  `lio_listio` also needs `SIGRTMIN`).  Adding these two unblocks the whole
  aio area.  *(The aio implementation itself was not reached — the tests
  cannot compile against substrate's headers.)*
- **message_queues: 10 fails** — `MQ_PRIO_MAX` undefined (missing from
  `<mqueue.h>`/`<limits.h>`); one needs `pthread_barrier_wait`.
- **signals: 41 fails** — `SIGRTMIN` / RT signals and `siginfo_t.si_value`
  absent (`sigqueue`, `sigwaitinfo` variants, RT-signal sigaction cases).
- **threads: 154 fails** — substrate `<pthread.h>`/libpthread lack
  `pthread_barrier_*`, `pthread_spin_*`, `pthread_rwlock_timed{rd,wr}lock`,
  `pthread_mutex_timedlock`, `pthread_mutexattr_setprioceiling/getprotocol`,
  `pthread_attr_setinheritsched`, `pthread_atfork`, ...
- **timers: 7**, **sched: 3**, **other: 6** — assorted (`clock_getcpuclockid`,
  `sched_setparam` SS members, `getpid` helper include, ...).

## Kernel PANICs — highest-value bugs (a test wedged/triple-faulted the kernel)

Each was isolated by the resume loop (kernel stalls/exits, reboot past it).

```
  other/fork/17-1                          [PANIC] : For the SCHED_FIFO and SCHED_RR scheduling policies, the child process inherits the policy and priority
  threads/pthread_setcanceltype/2-1        [PANIC] : Test pthread_setcanceltype The cancelability type of a newly created thread is PTHREAD_CANCEL_DEFERRED.
  sched/sched_setparam/20-1                [PANIC] : Test that the underlying kernel-scheduled entities for the process contention scope threads have their scheduling parameters changed to the
  sched/sched_setparam/21-1                [PANIC] : Test that the underlying kernel-scheduled entities for the process contention scope threads that are created after this call completes inher
  sched/sched_setparam/21-2                [PANIC] : Test that the underlying kernel-scheduled entities for the process contention scope threads that are created after this call completes inher
  sched/sched_setscheduler/15-2            [PANIC] : Test that the underlying kernel-scheduled entities for the process contention scope threads have their scheduling parameters changed to the
  sched/sched_setscheduler/17-2            [PANIC] : Test that the policy and scheduling parameters remain unchanged when the sched_ss_low_priority member is not within the inclusive priority r
  sched/sched_setscheduler/22-1            [PANIC] : Test that the underlying kernel-scheduled entities for the process contention scope threads that are created after this call completes inher
  sched/sched_setscheduler/22-2            [PANIC] : Test that the underlying kernel-scheduled entities for the process contention scope threads that are created after this call completes inher
  semaphores/sem_getvalue/2-1              [PANIC] : When semaphore is locked, then the value returned by sem_getvalue is zero. #define TEST "2-1"
  semaphores/sem_init/3-1                  [PANIC] : This test case illustrate the semaphore is shared between processes when pshared value is non-zero.
  semaphores/sem_unlink/4-2                [PANIC] : sem_unlink will return -1 and set errno to ENOENT when the named semaphore does not exist.
  mmap/shm_open/23-1                       [PANIC] : Test that the check for the existence of the shared memory object and the creation of the object if it does not exist is atomic with respect
  signals/sigaction/10-1                   [PANIC] : Test assertion #10 by verifying that SIGCHLD signals are sent to a parent when their children are stopped.
  signals/sigaction/22-26                  [PANIC] : Test case for assertion #22 of the sigaction system call that verifies that if the SA_NODEFER flag is set for a given signal, then when the
```

**Biggest kernel bug:** `sched_setparam` / `sched_setscheduler` with an
out-of-range / invalid `sched_priority` triple-fault the kernel instead of
returning `EINVAL` (the 20-*/21-*/22-* assertion cluster).  On the previous
kernel this was an even larger cluster (`sched_setparam/23-1..23-7` also
faulted); several now merely fail, but the range-check path still panics.
The ksem area adds new wedges (`sem_getvalue/2-1`, `sem_init/3-1` pshared,
`sem_unlink/4-2`), and `shm_open`/`sigaction`/`fork`/`pthread_setcanceltype`
each have one panicking case.

## Test-process CRASHes (SIGSEGV / abort)

```
  mmap/shm_open/1-1                        [CRASH] : Test that the shm_open() function establish a connection between a shared memory object and a file descriptor.
  mmap/shm_open/14-2                       [CRASH] : Test that the file is open for read acces when the applications specify the value of O_RDWR.
  mmap/shm_open/28-1                       [CRASH] : Test that the state of the shared memory object, including all data associated with the shared memory object, persists until the shared memo
  mmap/shm_open/28-3                       [CRASH] : Test that the state of the shared memory object, including all data associated with the shared memory object, persists until all open refere
  signals/sigpending/1-2                   [CRASH] : Test that the sigpending() function stores the set of signals that 1) Block three signals from delivery to a signal handler.
  signals/sigpending/1-3                   [CRASH] : Test that the sigpending() function stores the set of signals that are blocked from delivery when there are signals blocked both
```

## Watchdog TIMEOUTs (test hung; kernel survived)

```
  timers/clock/1-1                         [TIMEOUT] : Test that clock() returns a clock_t containing the processor time since a specific point in time.
  other/fork/12-1                          [TIMEOUT] : The child process is created with no pending signal The steps are:
  other/fork/8-1                           [TIMEOUT] : tms_{,c}{u,s}time values are set to 0 in the child process. The steps are:
  mmap/mmap/24-1                           [TIMEOUT] : The mmap() function shall fail if: [ENOMEM] MAP_FIXED was specified, and the range [addr,addr+len)
  message_queues/mq_timedsend/16-1         [TIMEOUT] : Test that if the message queue is full and O_NONBLOCK is not set, mq_timedsend() will block until abs_timeout is reached.
  timers/nanosleep/10000-1                 [TIMEOUT] : Test nanosleep() on a variety of valid and invalid input parameters. For valid parameters, if the seconds spent is within OKSECERR, the
  threads/pthread_cancel/1-1               [TIMEOUT] : Test pthread_cancel Shall request that 'thread' shall be canceled. The target thread's
  threads/pthread_cancel/2-1               [TIMEOUT] : Test pthread_cancel When the cancelation is acted on, the cancelation cleanup handlers for
  threads/pthread_cancel/3-1               [TIMEOUT] : Cancellation steps happen asynchronously with respect to the pthread_cancel(). The return status of pthread_cancel()
  threads/pthread_cleanup_push/1-2         [TIMEOUT] : void pthread_cleanup_push(void (*routine) (void*), void *arg); Shall push the specified cancelation cleanup handler routine onto the calling
  threads/pthread_cond_init/4-1            [TIMEOUT] : The function fails and returns ENOMEM if there is not enough memory. The steps are:
  threads/pthread_join/3-1                 [TIMEOUT] : Test that pthread_join() When pthread_join() returns successfully, the target thread has been
  threads/pthread_mutex_init/1-2           [TIMEOUT] : If the mutex attribute pointer passed to pthread_mutex_init is NULL, the effects on the mutex are the same as if a default mutex attribute o
  threads/pthread_mutex_init/3-2           [TIMEOUT] : The macro PTHREAD_MUTEX_INITIALIZER can be used to initialize mutexes that are statically allocated.
  threads/pthread_mutex_init/5-1           [TIMEOUT] : The function fails and returns ENOMEM if there is not enough memory. The steps are:
  threads/pthread_once/3-1                 [TIMEOUT] : Test pthread_once() The pthread_once() function is not a cancelation point. However if
  threads/pthread_rwlock_rdlock/4-1        [TIMEOUT] : Test pthread_rwlock_rdlock(pthread_rwlock_t * rwlock) If a signal is delivered to a thread waiting for a read-write lock for reading, upon
  threads/pthread_rwlock_wrlock/3-1        [TIMEOUT] : Test pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) It may fail if:
  threads/pthread_setcanceltype/1-1        [TIMEOUT] : Test pthread_setcanceltype Atomically sets the cancelability type to 'type' and returns the old
  signals/sigpause/1-2                     [TIMEOUT] : This program verifies that sigpause() suspends the calling process until it receives a signal.
  signals/sigpause/2-1                     [TIMEOUT] : This program verifies that sigpause() restores sig to the signal mask before returning.
  signals/sigpause/4-1                     [TIMEOUT] : This program verifies that sigpause() returns -1 and sets errno to EINVAL if passed an invalid signal number.
```

## FAIL roadmap — every failing test, grouped by interface

Interface, FAIL count, the assertion it checks, and the failing subtest ids.

```
signals/sigaction            78 FAIL : If SA_NODEFER is not set in sa_flags, the caught signal is added to the thread's signal mask during the handler execution.
                             ids: 23-1 23-10 23-11 23-12 23-13 23-14 23-15 23-16 23-17 23-18 23-19 23-2 23-20 23-21 23-22 23-23 23-25 23-26 23-3 23-4 23-5 23-6 23-7 23-8 23-9 4-1 4-10 4-11 4-12 4-13 4-14 4-15 4-16 4-17 4-18 4-2 4-20 4-21 4-22 4-23 4-24 4-25 4-26 4-27 4-28 4-29 4-3 4-30 4-31 4-32 4-33 4-34 4-36 4-37 4-38 4-39 4-4 4-40 4-41 4-42 4-43 4-44 4-45 4-46 4-47 4-48 4-49 4-5 4-50 4-51 4-52 4-6 4-7 4-8 4-9 23-24 4-19 4-35 
mmap/mmap                     7 FAIL : The mmap() function shall fail if: ML [EAGAIN] The mapping could not be locked in memory,
                             ids: 18-1 19-1 23-1 24-2 31-1 32-1 7-3 
timers/clock_settime          4 FAIL : Test that clock_settime() sets errno to EINVAL if clock_id does not specify a known clock.
                             ids: 17-1 17-2 19-1 20-1 
signals/pthread_sigmask       4 FAIL : 1. Call pthread_sigmask with a randomly generated value of how that is checked to make sure it does not equal any of the three def
                             ids: 16-1 4-1 6-1 9-1 
sched/sched_setscheduler      4 FAIL : Test that sched_setscheduler() sets errno == EINVAL when the sched_priority member is not within the inclusive priority range for 
                             ids: 19-1 19-5 20-1 21-1 
sched/sched_getscheduler      4 FAIL : Test that the scheduling policy is returned for the calling process when pid = 0
                             ids: 1-1 3-1 4-1 7-1 
sched/sched_getparam          4 FAIL : Test that sched_getparam() function return the scheduling parameters of a process specified by pid in the sched_param structure po
                             ids: 1-1 2-1 3-1 4-1 
sched/sched_get_priority_min  4 FAIL : Test that sched_get_priority_min() returns the minimum value on success for SCHED_RR policy.
                             ids: 1-1 1-2 1-4 2-1 
sched/sched_get_priority_max  4 FAIL : Test that sched_get_priority_max() returns the maximum value on success for SCHED_RR policy.
                             ids: 1-1 1-2 1-4 2-1 
message_queues/mq_timedreceive  4 FAIL : mq_timedreceive() test plan: mq_timedreceive() will fail with EINVAL if message queue is
                             ids: 17-1 17-2 17-3 8-1 
timers/timer_create           3 FAIL : Test that timer_create() creates a timer. 1. Set up sigevent structure to send signal SIGTOTEST on timer
                             ids: 1-1 16-1 3-1 
timers/clock_nanosleep        3 FAIL : Test that clock_nanosleep() causes the current thread to be suspended until the time interval in rqtp passes if TIMER_ABSTIME is n
                             ids: 1-1 1-5 13-1 
threads/pthread_cancel        3 FAIL : Test pthread_cancel Shall request that 'thread' shall be canceled. The target thread's
                             ids: 1-3 2-2 2-3 
signals/sigwait               3 FAIL : Test that the sigwait() function. If no signal in 'set' is pending at the time of the call, the thread shall be suspended until on
                             ids: 1-1 6-1 8-1 
signals/signal                3 FAIL : This program tests the assertion that the signal function shall return the function name of the last signal handler that was assoc
                             ids: 5-1 6-1 7-1 
signals/sigaltstack           3 FAIL : This program tests the assertion that sigaltstack() returns 0 upon successful completion.
                             ids: 10-1 11-1 12-1 
semaphores/sem_timedwait      3 FAIL : The process would be blocked, and the timeout parameter is secified in nanoseconds field value less than zero. Should
                             ids: 6-1 6-2 9-1 
timers/clock_gettime          2 FAIL : Test that clock_gettime() sets errno to EINVAL if clock_id does not specify a known clock.
                             ids: 8-1 8-2 
timers/clock_getres           2 FAIL : Test that clock_getres() returns -1 on failure. #define INVALIDCLOCKID 99999
                             ids: 5-1 6-2 
threads/pthread_setschedparam  2 FAIL : adam.li@intel.com Set the sched parameter with pthread_setschedparam() then get
                             ids: 1-1 4-1 
threads/pthread_setcancelstate  2 FAIL : Test pthread_cancelstate Atomically sets the cancelability state to 'state' and returns the old
                             ids: 1-1 2-1 
threads/pthread_attr_setdetachstate  2 FAIL : Test that pthread_attr_setdetachstate() If the thread is created detached, then use of the ID of the newly created
                             ids: 2-1 4-1 
signals/sigtimedwait          2 FAIL : Test that if the signal specified by set does not become pending, the sigtimedwait() function shall wait for the time interval spe
                             ids: 1-1 6-1 
signals/sigprocmask           2 FAIL : After sigprocmask() is called on an invalid how it should return -1 and set errno to EINVAL.
                             ids: 17-1 6-1 
signals/sigpending            2 FAIL : Test that the sigpending() function stores the set of signals that 1) Block three signals from delivery.
                             ids: 1-1 2-1 
other/strftime                2 FAIL : This test case will cover all the conversion specifiers that are supported in strftime().
                             ids: 1-1 2-1 
other/fsync                   2 FAIL : The fsync() function shall fail if: [EBADF] The fildes argument is not a valid descriptor.
                             ids: 5-1 7-1 
other/fork                    2 FAIL : Tests that file locks are not inherited by the child process after a fork. static int child(int fd)
                             ids: 11-1 16-1 
mmap/shm_open                 2 FAIL : Test that the FD_CLOEXEC file descriptor flag associated with the new file descriptor is set
                             ids: 11-1 5-1 
mmap/mlockall                 2 FAIL : Test that the mlockall() function set errno = EINVAL if the flags argument is zero.
                             ids: 13-1 13-2 
message_queues/mq_open        2 FAIL : Test that mq_open() fails with ENAMETOOLONG if the name parameter's length is greater than PATH_MAX.
                             ids: 27-1 27-2 
timers/nanosleep              1 FAIL : Regression test motivated by an LKML discussion. Test that nanosleep() can be interrupted and then continue.
                             ids: 3-2 
timers/functional             1 FAIL : 
                             ids: twopsetclock 
threads/pthread_testcancel    1 FAIL : Test pthread_testcancel Atomically sets the cancelability type to 'type' and returns the old
                             ids: 1-1 
threads/pthread_setcanceltype  1 FAIL : Test pthread_setcanceltype Atomically sets the cancelability type to 'type' and returns the old
                             ids: 1-2 
threads/pthread_mutexattr_getpshared  1 FAIL : Test that pthread_mutexattr_getpshared() It shall obtain the value of the process-shared attribute from 'attr'.
                             ids: 1-2 
threads/pthread_getschedparam  1 FAIL : adam.li@intel.com The pthread_getschedparam() function shall retrieve the scheduling
                             ids: 1-2 
threads/pthread_exit          1 FAIL : Test that pthread_exit() Any cancelation cleanup handlers that have been pushed and not yet popped
                             ids: 2-1 
threads/pthread_detach        1 FAIL : Test that pthread_detach() Upon failure, it shall return an error number:
                             ids: 4-1 
threads/pthread_create        1 FAIL : Test that pthread_create() creates a new thread with attributes specified by 'attr', within a process.
                             ids: 1-3 
threads/pthread_cleanup_push  1 FAIL : void pthread_cleanup_push(void (*routine) (void*), void *arg); Shall push the specified cancelation cleanup handler routine onto t
                             ids: 1-1 
threads/pthread_attr_setscope  1 FAIL : Test pthread_attr_setscope() 1. Initialize a pthread_attr_t object using pthread_attr_init()
                             ids: 4-1 
signals/sigwaitinfo           1 FAIL : This program tests the assertion that if the info parameter is not NULL, then the selected signal number shall be stored in the si
                             ids: 5-1 
signals/sigsuspend            1 FAIL : Assumption: The test assumes that this program is run under normal conditions, and not when the processor and other resources are 
                             ids: 3-1 
signals/sigset                1 FAIL : This program tests the assertion that if disp is SIG_HOLD, then the signal shall be added to the process's signal mask.
                             ids: 6-1 
signals/sigpause              1 FAIL : This program verifies that sigpause() returns -1 and sets errno to EINTR when it returns.
                             ids: 3-1 
semaphores/sem_unlink         1 FAIL : Trying to unlink a semaphore which it doesn't exist. It give an ERROR: ENOENT.
                             ids: 4-1 
semaphores/sem_open           1 FAIL : If a process calls sem_open several times with the same name, the same adress must be returned as long as the semaphore
                             ids: 15-1 
mmap/munmap                   1 FAIL : The munmap() function shall fail if: [EINVAL] Addresses in the range [addr,addr+len)
                             ids: 8-1 
mmap/mlock                    1 FAIL : Test that the mlock() function sets errno = EPERM if the calling process does not have the appropriate privilege to perform the re
                             ids: 12-1 
message_queues/mq_timedsend   1 FAIL : Test that mq_timedsend() will return EINVAL if the message queue is full and abs_timeout has a tv_nsec < 0 or >= 1000 million.
                             ids: 19-1 
message_queues/mq_close       1 FAIL : mq_close test plan: 1. Create pipes to communicate with child process
                             ids: 2-1 
```

## Notable clusters / observations

- **sched is almost entirely broken**: 2 PASS of 66.  The whole query family
  (`sched_get_priority_max/min`, `sched_getparam`, `sched_getscheduler`) FAILs,
  the `sched_setparam`/`sched_setscheduler` range-check paths PANIC, and 12
  are UNSUPPORTED (SCHED_SPORADIC).  Highest-leverage area to fix.
- **signals/sigaction**: 78 FAIL, concentrated in assertion #4 (signal added
  to the mask during its own handler unless `SA_NODEFER`) and #23; plus 2
  kernel panics (`10-1` SIGCHLD-on-stop, `22-26` `SA_NODEFER`) and 2 CRASHes
  (`sigpending/1-2,1-3`).
- **mmap/shm_open**: 4 CRASH + 1 PANIC — POSIX shared memory objects are
  fragile.  `mmap` itself has 7 FAIL (error-path `EAGAIN`/`ENOMEM`/`EACCES`).
- **timers**: 43 UNRESOLVED (many clock/timer tests bail before asserting) +
  16 FAIL (error-path `EINVAL` on bad `clock_id`, `timer_create` signal
  delivery).
- **message_queues**: strong — 101 / 111 PASS.  Fails are error-path
  (`mq_timedreceive` `EINVAL`/`EMSGSIZE`, `mq_open` `27-*`) plus one blocking
  `mq_timedsend` TIMEOUT.
- **semaphores**: 59 / 73 PASS including named `sem_open`/`close`/`unlink`;
  the 3 PANICs are process-shared / named-sem edge cases.

---

# Run 2 — kernel `07ecdb34`

Second on-substrate OPTS run, after a large round of conformance fixes merged
on `main`.  Same suite (LTP `01d0eecd`), same harness, same toolchain
(`i386-unknown-substrate-gcc` 16.1.0, tests linked `-lrt -lpthread -lsys -lm`).

- Kernel: `sys/kernel.multiboot` at `main` = `07ecdb34` (clean
  `make -C sys clean && make -C sys`).
- Libs: `libc.so.0`, `libpthread.so.0`, `librt.so.0` rebuilt at HEAD and
  mirrored into the sysroot + gcc `include-fixed` (`signal.h` / `pthread.h` /
  `mqueue.h` / `aio.h` / `semaphore.h`) so tests build+link against the new
  surface.  **All three shared objects** (not just libpthread/librt) are
  injected into the disposable boot copy's `/lib` — the clock / sched
  error-path, `sigqueue` / RT-signal and `pthread_atfork` fixes live in
  `libc.so.0`, so `run-baseline.sh` now injects it too (harness change).
- Accelerator: **`ACCEL=tcg`** (correct).  A stray `ACCEL=kvm` run that
  collided mid-measurement was discarded — under KVM the documented i386
  post-signal coherence bug manufactured spurious stalls (e.g.
  `timers/clock_settime/17-1`, a plain FAIL, reported as a kernel PANIC).
  The numbers below are the TCG run only.
- Boots: **8** (one clean pass + 7 panic-resume reboots — see PANIC list).

## Fixes merged since Run 1 (context)

`clock_*` EINVAL error-paths (`c991e688`); `mq_open`/`mq_unlink` ENAMETOOLONG
(`26628883`); real validating SCHED syscalls 416–422 (`01e610f4`,`2f05b87e`);
`sigaction` assertion #4/#23, `sigpending`, RT signals (`SIGRTMIN`=29/
`SIGRTMAX`=30) + `si_value` + `sigqueue(2)` (`0afb92fc`); pthread barriers /
spinlocks / timed locks / atfork / inherit-sched + `MQ_PRIO_MAX` /
`_SC_ASYNCHRONOUS_IO` (`07ecdb34`).

## Build coverage (1452 build-OK / 133 build-fail of 1585; runnable 1451)

Build-fails dropped **294 → 133**.  Per area (Run 1 → Run 2 build-fail):

```
AREA             BUILD-OK  BFAIL(R1→R2)
threads               277  154 → 81
signals               624   41 → 30
semaphores             73    1 →  1
timers                 95    7 →  7
mmap                   94    0 →  0
sched                  65    3 →  3
message_queues        121   10 →  0
aio                    67   72 →  5
other                  36    6 →  6
```

`aio` (0 → 67 runnable) and `message_queues` (10 → 0 fails) unblocked wholesale
by `_SC_ASYNCHRONOUS_IO` / `MQ_PRIO_MAX`; `threads` and `signals` shrank as the
pthread-timed/barrier/spin and RT-signal headers landed.  The remaining 133 are
still real, un-patched substrate gaps, dominated by missing `<unistd.h>`
sysconf constants (`_SC_REALTIME_SIGNALS` ×28, `_SC_THREAD_STACK_MIN` ×16,
`_SC_THREAD_PROCESS_SHARED` ×14, `_SC_CPUTIME`/`_SC_MONOTONIC_CLOCK`/
`_SC_CLOCK_SELECTION`/`_SC_PRIORITIZED_IO`/`_SC_AIO_MAX`/`_SC_SIGQUEUE_MAX`/
`_SC_SEM_NSEMS_MAX`/`_SC_MAPPED_FILES`), missing pthread decls
(`pthread_attr_set/getschedpolicy`, `pthread_rwlockattr_init`,
`pthread_condattr_set/getpshared`, `pthread_attr_getstack`,
`pthread_getattr_np`, `pthread_getcpuclockid`), `PTHREAD_STACK_MIN`, and
`_POSIX_SPORADIC_SERVER` (SCHED_SPORADIC).

## Per-area result

```
AREA           PASS  FAIL UNRES UNSUP UNTST TMOUT CRASH PANIC  TOTAL
threads         223    26     2     8     0    13     1     3    276
signals         509    20    91     0     0     3     0     1    624
semaphores       63     3     5     0     1     0     0     1     73
timers           41     9    37     5     1     2     0     0     95
mmap             39     9    31     4     5     1     4     1     94
sched            41     1     4    19     0     0     0     0     65
message_queues  117     2     1     0     0     1     0     0    121
aio              41    20     0     0     2     3     0     1     67
other            29     4     2     0     0     1     0     0     36
------------------------------------------------------------------------
TOTAL ran=1451  PASS=1103 FAIL=94 UNRESOLVED=173 UNSUPPORTED=36 UNTESTED=9 TIMEOUT=24 CRASH=5 PANIC=7
```

## Delta vs Run 1

```
                Run 1    Run 2    delta
PASS              818     1103     +285
FAIL              187       94      -93
UNRESOLVED        206      173      -33
UNSUPPORTED        28       36       +8
UNTESTED            7        9       +2
TIMEOUT            22       24       +2
CRASH               6        5       -1
PANIC              15        7       -8
build-fails       294      133     -161
runnable         1289     1451     +162
```

Per-area PASS climb: threads 163→223 (+60), signals 406→509 (+103),
semaphores 59→63 (+4), timers 28→41 (+13), mmap 35→39 (+4), **sched 2→41
(+39)**, message_queues 101→117 (+16), **aio 0→41 (+41)**, other 24→29 (+5).

Headline confirmations (direct row checks): `signals/sigaction` FAIL **78 → 0**
(the assertion-#4/#23 mask-during-handler fix); `signals/sigpending` CRASH
**2 → 0**; the `sched` query family (`sched_getscheduler`/`getparam`/
`get_priority_min`/`get_priority_max`) FAIL **16 → 0**; `sched` PANIC
**7 → 0**; `aio` PASS **0 → 41**.

## PANIC 15 → 7 (kernel wedges)

Of Run 1's 15 panics, **12 no longer wedge the kernel**: 4 now PASS
(`other/fork/17-1`, `semaphores/sem_getvalue/2-1`, `semaphores/sem_unlink/4-2`,
`signals/sigaction/22-26` — the `SA_NODEFER` case), 7 `sched` panics now return
cleanly as UNSUPPORTED (`sched_setparam/20-1`,`21-1`,`21-2`;
`sched_setscheduler/15-2`,`17-2`,`22-1`,`22-2` — the range-check no longer
triple-faults), and 1 downgraded to FAIL (`threads/pthread_setcanceltype/2-1`).

**3 carry over unchanged** and **4 are new** (all in code that only started
building this run — aio, pthread spin/mutex):

```
  aio/aio_error/2-1          [PANIC] NEW  : aio_error while an aio_read is in progress
  threads/pthread_mutex_lock/4-1 [PANIC] NEW : (newly-buildable mutex path)
  threads/pthread_spin_init/2-1  [PANIC] NEW : pthread_spin_init (spinlocks now build)
  threads/pthread_spin_lock/3-1  [PANIC] NEW : pthread_spin_lock (deterministic wedge)
  semaphores/sem_init/3-1    [PANIC] carry: pshared sem shared between processes
  mmap/shm_open/23-1         [PANIC] carry: shm_open existence+creation atomicity
  signals/sigaction/10-1     [PANIC] carry: SIGCHLD sent to parent on child stop
```

The aio/threads wedges are partly **non-deterministic** (a discarded early run
wedged at `pthread_condattr_init/3-1` and `aio_suspend/4-1` instead) — a
timing-dependent corruption in the newly-reachable pthread-primitive / aio
kernel paths, so the attributed culprit is the first test after the wedge, not
necessarily the sole cause.  `sem_init/3-1`, `shm_open/23-1` and
`sigaction/10-1` reproduce at the same points as Run 1 (deterministic).

## Test-process CRASHes (5)

```
  mmap/shm_open/1-1          [CRASH] carry : shm_open connects object <-> fd
  mmap/shm_open/14-2         [CRASH] carry : O_RDWR open-for-read
  mmap/shm_open/28-1         [CRASH] carry : shm object persists until unlink
  mmap/shm_open/28-3         [CRASH] carry : shm object persists until last close
  threads/pthread_atfork/3-2 [CRASH] NEW   : atfork handler ordering (now builds)
```

The two Run 1 `signals/sigpending` crashes (`1-2`,`1-3`) are **gone** (now PASS).

## Watchdog TIMEOUTs (24)

```
  threads/pthread_mutex_init      1-2 3-2 5-1
  threads/pthread_cancel          1-1 2-1 3-1
  signals/sigpause                1-2 2-1 4-1
  threads/pthread_atfork          1-1 2-1        (NEW area)
  aio/aio_suspend                 1-1 4-1        (NEW area)
  threads/pthread_setcanceltype   1-1
  threads/pthread_rwlock_wrlock   3-1
  threads/pthread_join            3-1
  threads/pthread_cond_init       4-1
  threads/pthread_cleanup_push    1-2
  timers/nanosleep                10000-1
  timers/clock                    1-1
  other/fork                      8-1
  mmap/mmap                       24-1
  message_queues/mq_timedsend     16-1
  aio/lio_listio                  1-1            (NEW area)
```

## FAIL roadmap — every failing test, grouped by interface (94)

```
aio/aio_cancel                    7 : 2-1 3-1 5-1 6-1 7-1 9-1 10-1
aio/lio_listio                    6 : 4-1 5-1 7-1 13-1 14-1 15-1
mmap/mmap                         5 : 7-3 18-1 23-1 24-2 31-1
signals/sigwait                   4 : 1-1 2-1 6-1 8-1
aio/aio_write                     4 : 2-1 8-2 9-1 9-2
timers/timer_create               3 : 1-1 3-1 16-1
threads/pthread_setschedparam     3 : 1-1 1-2 4-1
threads/pthread_cancel            3 : 1-3 2-2 2-3
signals/sigwaitinfo               3 : 2-1 5-1 9-1
signals/sigtimedwait              3 : 1-1 4-1 6-1
signals/signal                    3 : 5-1 6-1 7-1
signals/sigaltstack               3 : 10-1 11-1 12-1
timers/clock_settime              2 : 7-1 7-2
timers/clock_nanosleep            2 : 1-5 13-1
threads/pthread_setcanceltype     2 : 1-2 2-1
threads/pthread_setcancelstate    2 : 1-1 2-1
threads/pthread_attr_setinheritsched 2 : 2-3 2-4
threads/pthread_attr_setdetachstate  2 : 2-1 4-1
other/strftime                    2 : 1-1 2-1
other/fork                        2 : 11-1 16-1
mmap/shm_open                     2 : 5-1 11-1
aio/aio_read                      2 : 11-1 11-2
timers/nanosleep                  1 : 3-2
timers/functional/timers/clocks   1 : invaliddates
threads/pthread_testcancel        1 : 1-1
threads/pthread_rwlock_timedwrlock 1 : 6-2
threads/pthread_rwlock_timedrdlock 1 : 6-2
threads/pthread_mutexattr_getpshared 1 : 1-2
threads/pthread_mutexattr_getprioceiling 1 : 1-1
threads/pthread_getschedparam     1 : 1-2
threads/pthread_exit              1 : 2-1
threads/pthread_detach            1 : 4-1
threads/pthread_create            1 : 1-3
threads/pthread_cleanup_push      1 : 1-1
threads/pthread_barrierattr_getpshared 1 : 2-1
threads/pthread_attr_setscope     1 : 4-1
signals/sigsuspend                1 : 3-1
signals/sigqueue                  1 : 4-1
signals/sigpause                  1 : 3-1
signals/pthread_sigmask           1 : 9-1
semaphores/sem_unlink             1 : 4-1
semaphores/sem_timedwait          1 : 9-1
semaphores/sem_open               1 : 15-1
sched/sched_setparam              1 : 9-1
mmap/munmap                       1 : 8-1
mmap/mlock                        1 : 12-1
message_queues/mq_timedreceive    1 : 8-1
message_queues/mq_close           1 : 2-1
aio/aio_fsync                     1 : 12-1
```

## Cycle-2 roadmap (highest leverage first)

- **aio is the new frontier**: 41 PASS / 20 FAIL of 67 on its first run.  The
  `aio_cancel` (7), `aio_write` (4), `lio_listio` (6) error/completion paths
  fail, `aio_suspend`/`lio_listio` block (3 TIMEOUT), and `aio_error/2-1`
  wedges the kernel.  Whole area was unmeasurable in Run 1.
- **pthread synchronisation primitives wedge the kernel**: `pthread_spin_*`,
  `pthread_mutex_lock/4-1` PANIC (partly non-deterministic → likely a real
  kernel memory-corruption bug in the newly-reachable path); `pthread_atfork`
  CRASH/TIMEOUT; several `pthread_cancel`/`cleanup`/`mutex_init` TIMEOUTs.
  Now that these compile, they are the biggest remaining kernel-stability risk.
- **sched flipped from broken to strong** (2→41 PASS): the query family and
  range-checks work; the residue is 19 UNSUPPORTED (SCHED_SPORADIC) and one
  `sched_setparam/9-1` FAIL.
- **signals is now mostly conformant** (509/624, sigaction 0 FAIL): residual
  FAILs are `sigwait`/`sigwaitinfo`/`sigtimedwait`/`sigaltstack`/`signal`
  return-value edge cases; 91 UNRESOLVED are RT-signal tests that bail before
  asserting; `sigpause` still TIMEOUTs (3).
- **mmap/shm_open remains fragile**: 4 CRASH + 1 PANIC + 2 FAIL — POSIX shared
  memory objects are the single most crash-prone interface.
- **Build coverage**: add the ~13 missing `_SC_*` sysconf constants and the
  handful of pthread attr/schedpolicy/rwlockattr/condattr-pshared declarations
  to unblock the remaining 133 build-fails (mostly `threads`/`signals`).

