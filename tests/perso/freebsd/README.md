# FreeBSD personality conformance tests

Tests that pin substrate's FreeBSD personality (`/perso/freebsd`,
`PERS_FREEBSD`) against a **real FreeBSD kernel**.  The method is the one
`tests/perso/netbsd/README.md` describes: build the test natively on a
FreeBSD/i386 host, make it pass there first -- until it does, the test is
what is wrong -- and then run the same binary under substrate.  Any
difference is a substrate bug.

## kqtest

`kqueue(2)` and `kevent(2)` (`sys/kern/kqueue.c`, kqueue(9)): the four
filters the kernel has (`EVFILT_READ`, `EVFILT_WRITE`, `EVFILT_USER`,
`EVFILT_TIMER`), `EV_ONESHOT` / `EV_DISPATCH` / `EV_CLEAR` /
`EV_ENABLE` / `EV_DISABLE`, error reporting through the event list and
through `errno`, timeouts, a trigger from another thread waking a wait,
and a closed descriptor taking its registration with it.

The reference run corrected the test twice when it was written, both
times against what the kernel then did: the flags that say what becomes
of a registration once reported are fixed when it is made (a second
`EV_ADD` does not change them), and every change replaces `udata`, an add
or not, unless it carries `EV_KEEPUDATA`.

    cc -O1 -Wall -Wextra -o kqtest kqtest.c -lpthread
    ./kqtest             # every line "ok", then "kqtest: PASS"

Then put the binary where the personality can run it (`freebsd.img` is a
local experiment image) and run it under substrate with the image mounted
at `/perso/freebsd`.  The output must be the same but for the measured
times.

## pdtest

Process descriptors (`sys/exec/perso/compat.c`): `pdfork(2)` giving the
parent a descriptor that is the child, `pdgetpid(2)` and `pdkill(2)` on
it, the last close of it killing a child that is still running -- and
not one made with `PD_DAEMON` -- and the child having no such descriptor
itself.  libcasper starts its helper with `pdfork`, so `wc`, `head` and
the rest of the base utilities that sandbox themselves depend on it.

    cc -O1 -Wall -Wextra -o pdtest pdtest.c
    ./pdtest             # every line "ok", then "pdtest: PASS"

12 checks, passing on FreeBSD 14.4 and under substrate.  One difference
is allowed for: FreeBSD hands a process whose descriptor was closed to
init, and substrate leaves it its parent's child, to be waited for or to
go when the parent does.  The test accepts either.

Build it on a FreeBSD host, as the method above says, for the reference
run.  It can also be built under substrate with the `cc` of a FreeBSD
tree (put the source in `/perso/freebsd/var/tmp`; a tree unpacked
without `<sys/procdesc.h>` is allowed for).  Until 2026-10-09 that
linked a file of the right length and all zeros: `ld.lld` writes its
output through a shared mapping and nothing else, and what was stored
through one was not written to the file when it was unmapped
(`tests/kern/mmapshared.c`).
