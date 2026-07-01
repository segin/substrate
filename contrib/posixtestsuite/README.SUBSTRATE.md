# Open POSIX Test Suite (OPTS)

The **Open POSIX Test Suite** — the POSIX conformance / functional test
collection maintained inside the Linux Test Project (LTP) at
`testcases/open_posix_testsuite/`.

Upstream: <https://github.com/linux-test-project/ltp>
Pinned commit: `01d0eecd694cab3b95db1394e327bdd40974493e` (see `fetch.sh`)
License: GPL-2.0 (see `build/ltp/testcases/open_posix_testsuite/COPYING`)
Substrate vendoring: shallow/sparse git clone + patch series (nothing in
`build/` is committed).

## What this port is

A conformance **baseline harness**, not a bug-fix.  It cross-compiles the
OPTS conformance + functional tests with the substrate toolchain, runs the
resulting binaries on substrate itself, and produces a per-area
PASS / FAIL / UNSUPPORTED / UNRESOLVED / TIMEOUT / CRASH / PANIC tally.
The FAILs/TIMEOUTs/CRASHes/PANICs are substrate conformance gaps — future
work, deliberately **not** fixed here.

## Build & run

```
./fetch.sh          # sparse-clone LTP, take the OPTS subtree
./build.sh          # cross-compile every buildable test with the
                    #   substrate cross gcc; stage bins + manifest into
                    #   dist-overlay/dist-posixtestsuite/opt/posixtestsuite
./run-baseline.sh   # build an ext2 test image, boot substrate headless
                    #   with it as a 2nd disk, run the suite, aggregate
```

Outputs (under `dist-overlay/dist-posixtestsuite/opt/posixtestsuite/`):

| file | meaning |
|------|---------|
| `bin/<area>/<iface>/<name>` | one ELF per buildable test |
| `manifest.txt` | `<area>/<iface>/<name>` per runnable test |
| `build-report.txt` | per-area BUILD-OK / BUILD-FAIL counts |
| `build-failures.txt` | each un-buildable test + first error line |
| `baseline-report.txt` | the on-substrate PASS/FAIL/... baseline |
| `baseline-results.psv` | raw `area\|name\|verdict\|rc` rows |

## Mechanics

Each OPTS test is a single `.c` that defines `test_main()`; upstream's
`lib/common.c` supplies `main()` and exits with a POSIX-test result code
(`include/posixtest.h`):

| code | meaning |
|------|---------|
| 0 | PTS_PASS |
| 1 | PTS_FAIL |
| 2 | PTS_UNRESOLVED |
| 4 | PTS_UNSUPPORTED |
| 5 | PTS_UNTESTED |
| 6 | PTS_NORESULT |

`build.sh` bypasses upstream's `generate-makefiles.sh` harness and drives
the cross-compile directly: it globs each interface directory, compiles
every `*.c` against substrate libc with `-Iinclude` and links
`-lpthread -lm`.  A compile/link failure is recorded (it usually means a
missing substrate API/type) and the test is dropped from the run manifest.

`run-baseline.sh` never mass-injects into `rootfs.img` (documented to
corrupt it).  Instead it:

1. `mke2fs -d` builds a **fresh** ext2 image populated from the staged
   `bin/` tree + `manifest.txt` — no loop-mount, no debugfs write cycles.
2. `cp --sparse=always` copies `rootfs.img` and injects a **single** file,
   `opts-driver.sh`, via one `debugfs write`.  The copy is booted with
   `snapshot=on` so the real image is never mutated.
3. Boots `sys/kernel.multiboot` with the test image as a second AHCI disk
   (`/dev/storage/sata1`), `init=/opts-driver.sh`.  QEMU needs `+rdrand`
   (kernel RNG) and captures the framed serial output to a file.
4. The guest driver runs each binary under a **watchdog timeout** (a
   userland hang becomes `TIMEOUT`, not a wedged run) and prints one
   framed line per test.
5. A test that **panics the kernel** is detected (dangling `OPTS|START`
   with no `RESULT`, or a serial stall), added to the skip-list, recorded
   as `PANIC`, and the run resumes on a fresh boot (up to `MAX_BOOTS`).

### Accelerator note (signals)

`run-baseline.sh` defaults to `ACCEL=tcg`.  There is a documented
QEMU **KVM** i386 single-byte-coherence bug that corrupts memory right
after a signal handler returns (repro under `-accel kvm`, clean under
`-accel tcg`).  Because a large part of this baseline is the *signals*
area, TCG is the trustworthy default; `ACCEL=kvm` is offered for speed but
signal-area FAILs collected under KVM should be re-confirmed under TCG
before being treated as substrate bugs.

## Areas built

Built (substrate ships the headers):

- **threads** — `pthread_*`
- **signals** — `sig*`, `kill`, `killpg`, `raise`, `pthread_kill`,
  `pthread_sigmask`
- **semaphores** — `sem_*`, incl. **named** `sem_open`/`sem_close`/
  `sem_unlink` (kernel ksem, main@5100a8e6)
- **timers** — `clock*`, `timer_*`, `nanosleep`, `time`
- **mmap** — `mmap`, `munmap`, `mlock*`, `munlock*`, `shm_open`,
  `shm_unlink`
- **sched** — `sched_*`
- **message_queues** — `mq_*`, `functional/mqueues` (kernel POSIX
  mqueue, main@5100a8e6)
- **aio** — `aio_*`, `lio_listio` (userspace librt)
- **other** (bonus) — `access`, `fork`, `fsync`, `getpid`, `str*`,
  time-conversion (`asctime`/`ctime`/`difftime`/`gmtime`/`localtime`/
  `mktime`/`strftime`)

As of main@5100a8e6 substrate ships `<mqueue.h>`, `<aio.h>`,
`struct sigevent`, named semaphores and a `librt`, so **no area is skipped
wholesale** any more.

### librt is dynamic

`librt` (mq_*/aio_* wrappers) is linked as `librt.so.0`.  `build.sh`
expects `librt.so` + `librt.so.0` in the toolchain sysroot; `run-baseline.sh`
injects the freshly built `librt.so.0` (and a `libpthread.so.0` new enough to
export `sem_open`) into the **disposable** boot copy's `/lib` via a handful
of `debugfs` writes — never into the real `rootfs.img`.

### Build failures within built areas

Recorded in `build-failures.txt` — missing substrate POSIX symbols, left
un-patched on purpose (they are the roadmap):

- **aio: 0/72 build** — `<unistd.h>` lacks `_SC_ASYNCHRONOUS_IO` (and
  `lio_listio` also needs `SIGRTMIN`); every aio test guards on it.
- **message_queues: 10** — `MQ_PRIO_MAX` undefined; one needs
  `pthread_barrier_wait`.
- **signals: 41** — `SIGRTMIN` / RT signals and `siginfo_t.si_value`
  absent (`sigqueue`, `sigwaitinfo` variants).
- **threads: 154** — `<pthread.h>`/libpthread lack `pthread_barrier_*`,
  `pthread_spin_*`, `pthread_rwlock_timed{rd,wr}lock`,
  `pthread_mutex_timedlock`, `pthread_mutexattr_setprioceiling/getprotocol`,
  `pthread_attr_setinheritsched`, `pthread_atfork`, ...
- **timers 7 / sched 3 / other 6** — assorted (`clock_getcpuclockid`,
  `sched_ss_*`, ...).

## No substrate source is modified

This port does not touch `lib/c`, `lib/pthread`, `include/`, or any kernel
header to make a test build or pass.  Missing interfaces surface as build
failures / FAIL / PANIC and become tracked follow-up work — see
`BASELINE.md` for the first-run tally and the itemized roadmap.
