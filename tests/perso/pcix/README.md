# PC/IX personality tests

`pcixtest.c` checks substrate's PC/IX personality (`/perso/pcix`,
`PERS_PCIX`, `pcix(4)`) from inside a PC/IX process.

There is no compiler for PC/IX but its own, so the test is compiled **by
PC/IX, under substrate**, with the `cc` on the distribution media: running
it also runs the compiler, assembler and linker of 1984, which is a test
of its own.  It is therefore written in the C of the time.

**It needs a PC/IX image**, which is built from distribution media that is
not in this repository (`tools/pcix/README.md`).  Not part of
`make -C tests`.

With the image mounted at `/perso/pcix` and `pcixtest.c` copied into its
`/tmp`:

    /perso/pcix/bin/sh -c 'cd /tmp && cc -o pcixtest pcixtest.c && ./pcixtest one two'

Every line reads `ok`, and the last is `pcixtest: PASS`.

## What it checks

52 checks against what `/usr/include/sys.s` on the media says the system
calls do, with the weight on where PC/IX differs from the Xenix/286 calls
that implement it:

- the arguments and environment a program starts with;
- the second result of a call, in DX: `getppid`, `geteuid`, `getegid`,
  the write end of a `pipe`, the high half of `time`;
- files: `creat`, `write`, `stat` and `fstat` (the structure's layout),
  `lseek` with a long in and out, `chmod`, `link`, `access`, the errors of
  `open` and `close` through the carry flag, and a directory read as
  16-byte entries;
- `uname`, `umask`;
- `fork` returning to the right place in each process with the registers a
  compiler keeps, `wait` storing the status through its pointer, `ECHILD`,
  `exece`;
- signals: `signal` returning the old disposition, a handler entered with
  the signal number and returning to the interrupted code with its
  registers, the disposition reset on delivery, an ignored signal, `alarm`
  and `pause`, **the same signal delivered a second time**, and the status
  of a child killed by a signal;
- `ustat` of the device a file is on, and of none;
- `lockf`: the whole file, refused to another process, and waited for;
- floating point.  This one is a check of the compiler as much as of the
  test: PC/IX writes an 8087 instruction with `INT` in place of its `WAIT`
  prefix, the compiler does its constant arithmetic that way, and until
  the personality knew to put the prefix back **a source with a
  floating-point number in it did not compile**.
