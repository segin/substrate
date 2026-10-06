/*
 * perso_svr3.c - AT&T UNIX System V Release 3 personality (i386).
 *
 * Runs the COFF programs of UNIX System V/386 Release 3 and the systems
 * built on it (Interactive UNIX among them).  The COFF loader
 * (exec/formats/coff.c) gives every i386 COFF executable this personality.
 *
 * Such a program enters the kernel with `lcall $7,$0` (perso_sysv386.c).
 * There is no call gate behind selector 7, so the call faults and reaches
 * the handle_trap hook.  Release 4 kept Release 3's calls as they were and
 * runs its binaries unchanged, so the calls are served by the Release 4
 * code (svr4/svr4_calls.c) under a description of their own.  No Release 3
 * program issues `int $0x80`, so there is no syscall table.
 */

#include <stddef.h>

#include <exec/perso/personality.h>
#include <exec/perso/svr4/svr4.h>

struct personality personality_svr3 = {
    .name = "AT&T UNIX SVR3",
    .id = PERS_SVR3,
    .syscall_table = NULL,
    .syscall_names = NULL,
    .syscall_fmts = NULL,
    .syscall_count = 0,
    .path_prefix = "/perso/svr3",
    .sendsig = svr3_sendsig,
    .handle_trap = svr3_handle_trap,
};
