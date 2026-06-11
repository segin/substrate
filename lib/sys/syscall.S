/*
 * syscall.S - i386 raw syscall entry point (native personality ABI)
 *
 * The kernel extracts syscall arguments from the user stack at
 * useresp[1..8] (see sys/arch/i386/syscall_abi.h); useresp[0] is
 * ignored.  The number is passed in %eax.
 *
 *   int  syscall(int nr, ...);
 *
 * Constraints: at int $0x80 the C args a1.. must sit at useresp[1..];
 * on return esp must land where a normal cdecl callee leaves it (on
 * the caller's first pushed value) so the caller's own arg cleanup is
 * balanced.  The previous version popped the number off and re-pushed
 * the return address, leaving esp one slot HIGH -- so every syscall()
 * leaked 4 bytes of stack.  A function's epilogue normally resets esp,
 * but a tight loop that calls syscall() WITHOUT returning (a futex
 * ping-pong) accumulates 4 bytes per call until esp climbs out of the
 * thread stack -> SIGSEGV.  Fix: bump esp past the return-address slot
 * for the int, then undo it before ret, leaving the stack balanced.
 */

.global syscall
.type syscall, @function

syscall:
    movl  4(%esp), %eax     /* %eax = syscall number (1st C arg) */
    addl  $4, %esp          /* esp -> &nr: a1.. land at useresp[1..] */
    int   $0x80
    subl  $4, %esp          /* esp -> &ret_addr */
    ret                     /* cdecl return; esp ends on the number slot (no leak) */

.size syscall, . - syscall

/* No executable stack required — see lib/c/arch/i386/crt0.S. */
    .section .note.GNU-stack,"",@progbits
