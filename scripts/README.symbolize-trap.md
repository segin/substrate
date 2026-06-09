# symbolize-trap.sh — resolve a kernel TRAP backtrace to source lines

Pairs the kernel `trap` cmdline flag (prints `TRAP: ... eip=0x...` + a user
backtrace on a fatal userspace SIGSEGV) with `/proc/<pid>/maps` to turn raw
userspace addresses into `library+offset  function file:line`.

Usage:
  1. Boot with `trap` in the kernel cmdline.
  2. Capture serial containing BOTH the `=====MAPS...=====`/`MAPS-END` block
     (cat /proc/<pid>/maps) AND the `TRAP:`/backtrace lines, into one file.
  3. scripts/symbolize-trap.sh <that-file>
It dumps each referenced library from rootfs.img and runs addr2line against
the (non-PIE) Xfbdev exe and the deterministically-based shared libraries.
