# System V Release 4 personality tests

Programs that run under substrate's SVR4 personality (`/perso/svr4`,
`PERS_SVR4`) and check it from inside.

They are **freestanding**: built with the host's compiler, making
Release 4's system calls themselves (`lcall $7,$0`, the number in `EAX`,
the arguments on the stack, carry for an error).  So they need no vendor's
libc, compiler or distribution media, and run on a stock image.  The ELF
loader takes an unbranded static program for a Release 4 one when it is
run from under `/perso/svr4`, which is all the installation there is:

    cc -m32 -static -nostdlib -fno-pie -no-pie -fno-stack-protector \
       -o ptytest ptytest.c
    mkdir -p /perso/svr4/tmp && cp ptytest /perso/svr4/tmp/
    /perso/svr4/tmp/ptytest

Not part of `make -C tests`: they have to run on substrate.

## ptytest

A pseudo-terminal taken the System V way (`docs/specs/sysv_pseudo_terminals.md`):
`/dev/ptmx`; what `ptsname()` does (`I_STR`/`ISPTM`, the master's minor
number); the slave not there before that and there after; `grantpt()`'s
helper `/usr/lib/pt_chmod`, if a vendor's tree is mounted at `/perso/svr4`
to supply one; `I_STR`/`UNLKPT`; the slave opened; `ptem`, `ldterm` and
`ttcompat` found and pushed, and a name that is no module refused;
`termios` and the `sgttyb` view of the same terminal agreeing; and data
across the pair in both directions with the reader already waiting.

It prints a line per check and ends `ptytest: PASS` or `ptytest: FAIL`,
with the exit status to match.  26 checks pass with Dell UNIX 2.2 mounted,
25 and a skip without.

One of its checks is for a fault that was the driver's and nobody's
personality: a `stat(2)` of the slave before it had ever been opened left
the pair dead, and the master read end-of-file from then on.  The test
does that `stat` deliberately, as `grantpt()` does.
