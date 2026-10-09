# Xenix/286 personality tests

`sigtest.c` checks signals under substrate's Xenix/286 personality
(`/perso/xenix`, `PERS_SCO_X286`) from inside a Xenix process.

There is no compiler for Xenix/286 but its own, so the test is compiled
**by Xenix, under substrate**, with the `cc` on the distribution media.
It is therefore written in the C of the time.

**It needs a Xenix/286 image**, which is built from distribution media
that is not in this repository (`tools/xenix/README.md`).  Not part of
`make -C tests`.

With the image mounted at `/perso/xenix` and `sigtest.c` copied into its
`/tmp`:

    /perso/xenix/bin/sh -c 'PATH=/bin:/usr/bin; export PATH; cd /tmp && cc -o sigtest sigtest.c && ./sigtest'

Every line reads `ok`, and the last is `sigtest: PASS`.

## What it checks

- `signal` giving back the old disposition, and `SIG_DFL` and `SIG_IGN`
  being told from a function by the offset alone: a small-model program
  passes no segment, and what is in the register a large-model one uses
  for it is whatever the caller left there;
- an ignored signal being ignored;
- the same signal delivered four times running, each reaching the
  function with its number and each returning to the interrupted code
  with the registers a compiler keeps;
- a second signal arriving as itself, the C library telling them apart
  by which of its trampolines the kernel entered;
- `alarm` and `pause` twice, which is two calls of `sleep(3)`.

Before 2026-10-09 this test died of `SIGILL` at its second line:
`signal(sig, SIG_IGN)` from a small-model program was taken for a
function at offset 1 of whatever segment the register held, and so was
every real handler.  Past that, the kernel pushed a frame the C library
does not return over, and left the signal blocked for a handler whose
return never reaches the kernel to unblock it.

## What it does not check

The large text model.  `cc -Ml` builds, and the kernel's side of a
large-model delivery is written from that library's code, but programs
of that model built by this `cc` do not start under substrate (they
loop in `stkgrow` and exit 100 before `main`), signals or no signals.
