/*
 * syscall.S - i386 raw syscall entry point (native personality ABI)
 *
 * Substrate's native personality (and BSD personalities — FreeBSD,
 * NetBSD, OpenBSD, SVR4) extract syscall arguments from the user
 * stack at offsets `useresp[1..8]`, NOT from %ebx/%ecx/%edx/...
 * (which is the Linux i386 convention).  See
 * sys/arch/i386/syscall_abi.h:i386_extract_syscall_args.
 *
 * The Substrate convention assumes the syscall was issued from a
 * libc stub that the user `call`ed: useresp[0] is the call's
 * return address, useresp[1..] are the C arguments laid out in
 * cdecl order.
 *
 * Strategy: rearrange the stack so `int $0x80` fires with that
 * exact layout — pop the syscall number off into %eax, push the
 * caller's return address back into the slot it vacated.  After
 * the syscall returns we `ret` normally; the caller cleans up
 * its own arguments per cdecl.
 *
 *   int  syscall(int nr, ...);
 *
 * Inbound stack:                Outbound (during int $0x80):
 *   [esp+0]  = ret_addr           [esp+0]  = ret_addr  (= useresp[0])
 *   [esp+4]  = nr                 [esp+4]  = arg0      (= useresp[1])
 *   [esp+8]  = arg0               [esp+8]  = arg1      (= useresp[2])
 *   [esp+12] = arg1               ...
 *   ...
 */

.global syscall
.type syscall, @function

syscall:
    popl  %ecx              /* ret_addr -> ecx; esp now points at nr   */
    popl  %eax              /* nr      -> eax; esp now points at arg0  */
    pushl %ecx              /* push ret_addr back; esp at ret_addr     */

    int   $0x80

    ret                     /* pop ret_addr, jump to caller            */

.size syscall, . - syscall
