/*
 * perso_svr4.c - AT&T UNIX System V Release 4 personality (i386).
 *
 * Runs ELF programs built for UNIX System V/386 Release 4: the AT&T
 * reference port and the releases made from it (Intel's, Dell's,
 * UnixWare).  The ELF loader gives a program this personality when it
 * names /usr/lib/libc.so.1 as its interpreter -- Release 4.0's libc is its
 * own dynamic linker -- or is an unbranded static executable under
 * /perso/svr4.
 *
 * Such a program enters the kernel with `lcall $7,$0`, as Xenix/386 does
 * and with the same conventions (perso_sysv386.c).  There is no
 * call gate behind selector 7, so the call faults and reaches the
 * handle_trap hook; the calls themselves are in svr4/svr4_calls.c.  No
 * Release 4 program issues `int $0x80`, so there is no syscall table.
 */

#include <stddef.h>

#include <exec/perso/personality.h>
#include <exec/perso/svr4/svr4.h>

struct personality personality_svr4 = {
    .name = "AT&T UNIX SVR4",
    .id = PERS_SVR4,
    .syscall_table = NULL,
    .syscall_names = NULL,
    .syscall_fmts = NULL,
    .syscall_count = 0,
    .path_prefix = "/perso/svr4",
    .native_dev = 1,
    .works_in_tree = 1,
    .sendsig = svr4_sendsig,
    .handle_trap = svr4_handle_trap,
};
