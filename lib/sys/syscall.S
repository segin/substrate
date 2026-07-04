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
 * balanced.
 *
 * SIGNAL SAFETY (critical): the i386 SysV ABI has NO red zone -- the
 * kernel builds a signal frame immediately below useresp when it
 * delivers a signal on the syscall's return.  So the return address
 * must NOT be left on the stack BELOW esp across the int, or a signal
 * that fires on return overwrites it with signal-frame bytes and the
 * ret jumps to garbage (typically 0).  The previous version did exactly
 * that: `addl $4,%esp` bumped esp past &ret_addr for the int, stranding
 * the return address at esp-4 -- delivering an aio/RT-signal storm on a
 * syscall return (OPTS lio_listio/3-1,4-1,10-1,14-1,15-1) clobbered it
 * and the raw syscall()'s ret jumped to 0 -> SIGSEGV.  (An earlier
 * version that popped+re-pushed the number instead leaked 4 bytes/call.)
 *
 * Fix: carry the return address in %ecx across the int.  %ecx is not a
 * native syscall argument (args come from the stack), the kernel's
 * trapframe save/restore preserves it across int $0x80, and if a signal
 * IS delivered on return the kernel's signal frame save/restore
 * preserves it too -- so it survives to the indirect return.  esp is
 * left on the number slot exactly as a plain cdecl `ret` would, so the
 * caller's argument cleanup stays balanced (no leak).
 */

.global syscall
.type syscall, @function

syscall:
    movl  4(%esp), %eax     /* %eax = syscall number (1st C arg) */
    popl  %ecx              /* %ecx = return address; esp -> &nr (a1.. at useresp[1..]) */
    int   $0x80
    jmp   *%ecx             /* cdecl return; esp left on the number slot (no leak) */

.size syscall, . - syscall

/* No executable stack required — see lib/c/arch/i386/crt0.S. */
    .section .note.GNU-stack,"",@progbits
