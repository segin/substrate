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
