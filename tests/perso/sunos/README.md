# SunOS personality tests

A program that runs under substrate's SunOS personality (`/perso/sunos`,
`PERS_SUNOS`, `sunos(4)`) and checks it from inside.

`suntest` is **freestanding**: built with the host's compiler, making
SunOS 4.0's system calls itself (`int $0xff`, the number in `%eax`, the
arguments on the stack, carry for an error).  It needs nothing of SunOS's
-- no libc, no `ld.so`, no distribution media -- and runs on a stock
image.  What makes it a Sun386i program to the kernel is its format, which
the build gives it:

| file | what it is |
| --- | --- |
| `suntest.c` | the test |
| `sun386.ld` | linker script: text at 0x10d0, data on the next page, as Sun's linker lays a program out |
| `mkcoff.py` | puts the i386 COFF file header, optional header (magic 0413) and section headers in front of the linked image |
| `build.sh` | runs the three: `cc -m32`, `nm`, `objcopy`, `python3` |

    tests/perso/sunos/build.sh            # writes tests/perso/sunos/suntest
    # on substrate:
    mkdir -p /perso/sunos/tmp && cp suntest /perso/sunos/tmp/
    /perso/sunos/tmp/suntest

It has to be run from `/perso/sunos/tmp`, a directory it makes files in.
Not part of `make -C tests`: it has to run on substrate.

## What it checks

46 checks, each against what the system's own headers and manual pages say
(`docs/specs/personality_targets.md`):

- `getpid`, `getuid`: the second result (`getppid`, `geteuid`) in `%edx`.
- Errors by the carry flag with 4.3BSD's numbers -- `ENOENT`, `EBADF`,
  `EEXIST`, `ENOTTY` -- and `EINVAL` for a call SunOS does not have.
- `fork`: `%edx` tells parent from child; `wait4` with a process ID of 0;
  a read of address 0 ends the child with `SIGSEGV`, page 0 being unmapped.
- The personality's own tree: `mkdir /tmp/suntest.d` makes
  `/perso/sunos/tmp/suntest.d`, a file created in it is found under both
  names and by a relative one after `chdir`.
- `struct stat` (type, mode, size), `fstat`, `statfs` of a plain file,
  `getdents` to the end of a directory.
- `dup` with 0100 set, which is `dup2`.
- `pipe`: both descriptors in registers, and data through it.
- `mmap` of `/dev/zero`: placed at or above 0x40000000, zero, writable;
  `munmap`; `brk` returning 0 and the memory being there; `getrlimit`.
- Signals: `sigvec`, `sigblock` and `sigsetmask` returning the mask before,
  a blocked signal held and then delivered, the handler entered as libc's
  `_sigtramp` is with the number and a `sigcontext`, and the return through
  `sigcleanup` with the mask restored.

It prints a line per check and `suntest: PASS` or `suntest: FAIL`; the
exit status says the same.  The line for the child that reads address 0 is
followed on the console by the kernel's report of that fault.

## With the distribution

The rest of what `sunos(4)` says runs -- the shells, the utilities, the
editors, the C compiler -- needs the SunOS 4.0.1 image `tools/sunos`
builds from media that are not in this repository, and has no test here.
