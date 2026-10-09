# Venix/86 personality tests

`venixtest.c` checks substrate's Venix personality (`/perso/venix`,
`PERS_VENIX`, `venix(4)`) from inside a Venix process.

Like `tests/perso/pcix/pcixtest.c`, which it is a copy of with Venix's
particulars put in, it is compiled **by the system under test**: Venix's own
`cc`, running under substrate.

**It needs a Venix image**, which is built from floppy images that are not
in this repository (`tools/venix/README.md`).  Not part of
`make -C tests`.

With the image mounted at `/perso/venix` and `venixtest.c` copied into its
`/tmp`:

    /perso/venix/bin/sh -c 'cd /tmp && cc -o venixtest venixtest.c && ./venixtest one two'

Every line reads `ok`, and the last is `venixtest: PASS`.

`venixext.c` is run the same way, as the superuser, and checks the calls
Venix added to Version 7 (36 checks):

    /perso/venix/bin/sh -c 'cd /tmp && cc -o venixext venixext.c && ./venixext'

- semaphores: test, test-and-set, clear, the per-program and the
  system-wide ones, and `semset` waiting for another process's `semclear`;
- `sdata`: anonymous shared data seen by a forked child, a file attached
  and stored into, ES moved within it and given back;
- `suspend` stopping and restarting a child (watched through the shared
  data it is counting in);
- `locking`: refused to a second process, granted after the unlock, waited
  for in mode 2, and by byte range;
- `phys` at the display adapter and refused below it; `lock`; `aiowait`.

`sdata` and `phys` work through the ES register, which C cannot name, so
the test calls two short routines it carries as machine code.

## What venixtest checks

46 checks.  The ones PC/IX's test makes -- arguments and environment, the
second result of a call, files and `stat`, a directory read as 16-byte
entries, `fork` and `wait`, `exece`, signals including the same one
delivered twice -- arrive here by `int 0xf1` with the arguments in
registers and the error number in CX, so they test that road.  And Venix's
own:

- `brk`, which is the kernel's here: an address above the bss is granted,
  one inside the program refused with `ENOMEM`;
- `st_mode` of a directory with the file type as the Sixth Edition had it;
- floating point, each 8087 instruction of which is preceded by
  `int 0xf4`;
- `isatty`, a `TIOCGETP` into six bytes of stack -- a longer `sgttyb`
  overwrites the caller's saved registers, which is how this was found.
