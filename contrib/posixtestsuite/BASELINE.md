# Substrate — Open POSIX Test Suite conformance baseline

First on-substrate OPTS conformance run.  This baseline is **step 1 toward
passing the suite**: every FAIL / TIMEOUT / CRASH / kernel-PANIC below is a
substrate conformance gap and a roadmap item.

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
