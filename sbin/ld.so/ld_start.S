/*
 * ld_start.S — entry point for /sbin/ld.so (Substrate dynamic linker).
 *
 * Phase 1 minimum viable bootstrap.  When the kernel detects a
 * PT_INTERP=/sbin/ld.so on a native ELF, it:
 *
 *   - loads ld.so as ET_DYN at base 0x40000000,
 *   - sets up the user stack with [argc, argv[], NULL, envp[], NULL,
 *     auxv[], AT_NULL] and pushes ld.so's startup state into auxv
 *     (AT_BASE, AT_ENTRY, AT_PHDR, AT_PHENT, AT_PHNUM, AT_PAGESZ),
 *   - jumps to ld.so's e_entry (this _start).
 *
 * %ESP at entry points at argc.  Native syscall ABI is stack-based
 * (REQ-23-not-relevant; see docs/specs/abi-i386.md §1): args at
 * [esp+4..], dummy slot at [esp+0], number in %eax, int $0x80.
 *
 * Phase 1 just proves the kernel-interp handoff works for native
 * binaries:
 *   - emit a single-line "ld.so: hello\n" via SYS_write,
 *   - locate AT_ENTRY in the auxv,
 *   - jump to it with %esp left exactly as the kernel handed it,
 *     so the program's _start sees a normal native stack.
 *
 * No PT_DYNAMIC parsing, no relocations on the program, no TLS.
 * Those land in Phase 2+.  Until they do, the program had better
 * be self-contained (no DT_NEEDED).
 */

#define SYS_write  4
#define SYS_exit   1

#define AT_NULL    0
#define AT_ENTRY   9   /* program entry point */

    .text
    .global _start
    .type _start, @function

_start:
    /* %ebp = saved esp = address of argc.  We must NOT clobber the
     * stack contents above ESP — the program's _start expects to
     * see argc / argv / envp / auxv exactly as the kernel built it. */
    movl %esp, %ebp

    /* Phase-1 banner: write(STDERR_FILENO, ld_so_msg, ld_so_msg_len)
     * via the native stack ABI.  Get our own load address via
     * call/pop so we don't need any relocations. */
    call .Lpc_anchor
.Lpc_anchor:
    popl %ebx                           /* %ebx = address of .Lpc_anchor */

    /* Push args right-to-left for SYS_write(2, msg, len): */
    leal ld_so_msg-.Lpc_anchor(%ebx), %ecx
    pushl $ld_so_msg_len_const          /* len */
    pushl %ecx                          /* buf */
    pushl $2                            /* fd = STDERR_FILENO */
    pushl $0                            /* dummy ret-addr slot */
    movl  $SYS_write, %eax
    int   $0x80
    addl  $16, %esp

    /* Walk auxv to find AT_ENTRY.
     *
     * Stack layout from %ebp (= original esp):
     *   [ebp+0]      argc
     *   [ebp+4]      argv[0]
     *   [ebp+...]    argv[N], NULL
     *   [ebp+...]    envp[0..M], NULL
     *   [ebp+...]    auxv: pairs of (a_type, a_un.a_val) terminated by AT_NULL.
     *
     * We need to skip over argc + argv (count = argc+1, pointer-sized) +
     * envp (count to NULL, pointer-sized) to land on auxv. */

    movl  %ebp, %edx
    movl  (%edx), %eax                  /* eax = argc */
    leal  4(%edx, %eax, 4), %edx        /* edx -> argv[argc] (== NULL slot) */
    addl  $4, %edx                      /* edx -> envp[0] */

    /* Skip envp until NULL terminator. */
.Lskip_envp:
    movl  (%edx), %eax
    addl  $4, %edx
    testl %eax, %eax
    jnz   .Lskip_envp                   /* loop while *p != NULL */

    /* %edx now points at the first auxv entry.  Walk pairs of
     * (type, value) until type == AT_NULL or AT_ENTRY. */
.Lscan_auxv:
    movl  (%edx), %eax                  /* a_type */
    testl %eax, %eax
    jz    .Lentry_missing               /* AT_NULL — no AT_ENTRY found */
    cmpl  $AT_ENTRY, %eax
    je    .Lentry_found
    addl  $8, %edx
    jmp   .Lscan_auxv

.Lentry_found:
    movl  4(%edx), %eax                 /* a_un.a_val = program entry */
    /* Restore %esp so the program sees the original stack. */
    movl  %ebp, %esp
    /* Zero callee-saved regs that the program's _start would expect
     * cleared per the native ABI (matches what the kernel does on
     * a fresh exec). */
    xorl  %ebp, %ebp
    xorl  %edx, %edx                    /* edx is "atexit" pointer slot */
    jmp   *%eax

.Lentry_missing:
    /* No AT_ENTRY in the auxv — kernel violated the contract.
     * Diagnostic + exit(127). */
    leal  err_no_entry-.Lpc_anchor(%ebx), %ecx
    pushl $err_no_entry_len_const
    pushl %ecx
    pushl $2
    pushl $0
    movl  $SYS_write, %eax
    int   $0x80
    addl  $16, %esp
    pushl $127
    pushl $0
    movl  $SYS_exit, %eax
    int   $0x80
    /* unreachable */
1:  hlt
    jmp 1b

    .size _start, . - _start

    .section .rodata
ld_so_msg:
    .ascii "ld.so: hello\n"
    .equ   ld_so_msg_len_const, . - ld_so_msg

err_no_entry:
    .ascii "ld.so: no AT_ENTRY in auxv\n"
    .equ   err_no_entry_len_const, . - err_no_entry
