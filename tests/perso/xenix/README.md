# Xenix/286 personality tests

Two programs that check substrate's Xenix/286 personality
(`/perso/xenix`, `PERS_SCO_X286`) from inside a Xenix process:
`sigtest.c`, signals, and `calltest.c`, the system calls that take
pointers.

There is no compiler for Xenix/286 but its own, so the tests are compiled
**by Xenix, under substrate**, with the `cc` on the distribution media.
They are therefore written in the C of the time.

**They need a Xenix/286 image**, which is built from distribution media
that is not in this repository (`tools/xenix/README.md`).  Not part of
`make -C tests`.

With the image mounted at `/perso/xenix` and the sources copied into its
`/tmp`:

    /perso/xenix/bin/sh -c 'PATH=/bin:/usr/bin; export PATH; cd /tmp && cc -o sigtest sigtest.c && ./sigtest'

Every line reads `ok`, and the last is `sigtest: PASS`; `calltest`
likewise.  **Build each three ways**, with no flags, with `-Mm` and
with `-Ml`: small, middle and large model, which are all the models
Xenix/286 has.  The first two reach the kernel by one convention and
the third by another (below).

## The two conventions

A small-data program (small and middle model) passes a system call's
arguments in BX, CX, SI and DI, a word each, a pointer being an offset
in DS.  A large-data one (the large model: `x_renv` has
`XE_LDATA`) pushes them as for any C function and traps with BX
pointing at the first, and a pointer among them is two words, offset
then selector.  The personality reads the second into the form of the
first, knowing from a table which arguments are pointers.

`calltest.c` is the test of that table: files, `stat`, `link`,
`chmod`, `time`, a pipe, `wait`, a record lock set with `fcntl` and
found by a child, and an `execve` whose child checks its arguments and
environment -- with the buffers in the data segment, on
the stack and on the heap, which in the large model are three segments.

Before 2026-10-09 a `cc -Ml` program did not start: its first call was
read from registers that held nothing, and its request to grow the
stack was answered with the address of the request.

## What sigtest checks

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

## What is not checked

`ioctl` beyond what the shell and `stty` do, and a record lock as seen
by a process that opened the file for itself: substrate keeps record
locks on the open file, so `calltest`'s second process is a child using
the descriptor it inherited.

There is no compact model to check.  Xenix/286's `cc` takes `-Ms`,
`-Mm` and `-Ml` and says of `-Mc` that it is an unknown substring, and
`/lib` has the three runtimes `Scrt0.o`, `Mcrt0.o` and `Lcrt0.o` and no
fourth.
