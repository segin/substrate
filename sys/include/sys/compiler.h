#ifndef _SYS_COMPILER_H
#define _SYS_COMPILER_H

#if defined(__GNUC__)
#define SUB_NODISCARD __attribute__((warn_unused_result))
#define SUB_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#define SUB_PURE __attribute__((pure))
#else
#define SUB_NODISCARD
#define SUB_NONNULL(...)
#define SUB_PURE
#endif

#ifdef __ASSEMBLER__
/* Assembly macros */
.macro extable_type_reg type:req reg:req
    .set .Lfound, 0
    .set .Lregnr, 0
    .irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15
        .ifc \reg, \rs
            .long \type + (.Lregnr << 8)
            .set .Lfound, 1
        .endif
        .ifc \reg, %\rs
            .long \type + (.Lregnr << 8)
            .set .Lfound, 1
        .endif
        .set .Lregnr, .Lregnr+1
    .endr
    .set .Lregnr, 0
    .irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d
        .ifc \reg, \rs
            .long \type + (.Lregnr << 8)
            .set .Lfound, 1
        .endif
        .ifc \reg, %\rs
            .long \type + (.Lregnr << 8)
            .set .Lfound, 1
        .endif
        .set .Lregnr, .Lregnr+1
    .endr
    .if  .Lfound == 0
        .error "extable_type_reg: bad register argument \reg"
    .endif
.endm
#else
/* Define the assembly macro for inline assembly as well */
__asm__(
".macro extable_type_reg type:req reg:req\n\t"
"    .set .Lfound, 0\n\t"
"    .set .Lregnr, 0\n\t"
"    .irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n\t"
"        .ifc \\reg, \\rs\n\t"
"            .long \\type + (.Lregnr << 8)\n\t"
"            .set .Lfound, 1\n\t"
"        .endif\n\t"
"        .ifc \\reg, %\\rs\n\t"
"            .long \\type + (.Lregnr << 8)\n\t"
"            .set .Lfound, 1\n\t"
"        .endif\n\t"
"        .set .Lregnr, .Lregnr+1\n\t"
"    .endr\n\t"
"    .set .Lregnr, 0\n\t"
"    .irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n\t"
"        .ifc \\reg, \\rs\n\t"
"            .long \\type + (.Lregnr << 8)\n\t"
"            .set .Lfound, 1\n\t"
"        .endif\n\t"
"        .ifc \\reg, %\\rs\n\t"
"            .long \\type + (.Lregnr << 8)\n\t"
"            .set .Lfound, 1\n\t"
"        .endif\n\t"
"        .set .Lregnr, .Lregnr+1\n\t"
"    .endr\n\t"
"    .if  .Lfound == 0\n\t"
"        .error \"extable_type_reg: bad register argument \\reg\"\n\t"
"    .endif\n\t"
".endm\n"
);
#endif

#endif /* _SYS_COMPILER_H */
