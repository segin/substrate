# NetBSD personality conformance tests

Tests that pin substrate's NetBSD personality (`/perso/netbsd`, `PERS_NETBSD`)
against the behaviour of a **real NetBSD kernel**, rather than against our own
reading of the man pages.

The method is a differential one, and it is the whole point of these tests:

1. Build the test **natively on a NetBSD 10.1/i386 host** (same arch as
   `netbsd.img` and the rest of the foreign binaries staged there, so it is a
   native compile, not a cross).
2. Run it **on that NetBSD host first** and make it go fully green there.  Until
   it does, the test — not the kernel — is what is wrong.  That green run is the
   oracle.
3. Stage the same binary into `netbsd.img` and run it under substrate.  Any
   difference from the oracle is a substrate conformance bug.

Step 2 is not optional.  When `lwptest` was first written its `_lwp_wait(0)`
case asserted `EDEADLK`; the NetBSD host immediately failed it with `ESRCH`,
which is what NetBSD's `lwp_wait()` actually returns for its `nfound == 0` case
(`EDEADLK` is reserved for "every LWP is simultaneously blocked in
`_lwp_wait`").  Reading the source then also showed `_lwp_wait()` on a
**detached** LWP is `EINVAL`.  Both were real substrate bugs, found only because
the reference ran first.

## lwptest

Exercises the LWP lifecycle directly against the raw `_lwp_*` syscalls (no
libpthread bookkeeping in the way):

| | case | expected |
|---|---|---|
| T1 | `_lwp_wait(0)` with no waitable sibling | `ESRCH` |
| T2 | `_lwp_create` + `_lwp_wait(0)` | any-LWP reap, `departed == lid` |
| T3 | `_lwp_create(LWP_DETACHED)` x200 | kernel self-reap, no join |
| T4 | `_lwp_create(LWP_SUSPENDED)` | parked until `_lwp_continue` |
| T5 | `_lwp_suspend` / `_lwp_continue` on a live LWP | progress stops / resumes |
| T6 | `_lwp_wait(lid)` on a detached LWP | `EINVAL` |

Raw LWPs get no libpthread TCB, so their entry points touch only volatile
globals and atomics before `_lwp_exit()` — no stdio or TLS from a raw LWP.

## opentest

Pins open(2) flag handling.  NetBSD uses the BSD flag numbering, substrate's
native `<sys/fcntl.h>` uses the Linux one, and only the access mode agrees — so
dispatching `open()` straight at `sys_open` mis-read every other bit.  The
damaging one: NetBSD's `O_CREAT` (0x200) is substrate's `O_TRUNC`, so a NetBSD
binary could never *create* a file.  Covers O_CREAT / O_EXCL / O_TRUNC /
O_APPEND.

Known gap not covered: NetBSD returns `EFTYPE` for `open(symlink, O_NOFOLLOW)`
where substrate returns `ELOOP`, and substrate has no `EFTYPE` to map onto.

### Build + run

On a NetBSD 10.1/i386 host:

    cc -O1 -Wall -Wextra -o lwptest lwptest.c
    ./lwptest            # must be ALL PASS — this is the reference

Then, on the substrate side, stage the binary into the image and run it under
the personality (`netbsd.img` is a local experiment image; a single-file
`debugfs -w` write is safe, mass injection is not):

    printf 'write lwptest /usr/local/bin/lwptest\n' | debugfs -w netbsd.img
    debugfs -w -R "sif /usr/local/bin/lwptest mode 0100755" netbsd.img

Boot substrate with `netbsd.img` as a second disk, mount it at `/perso/netbsd`,
and run `/perso/netbsd/usr/local/bin/lwptest`.  Output must match the host's,
line for line.
