# i386 Signal Frame Specification

## Overview

Substrate's i386 signal delivery builds a frame on the target user stack and
redirects the saved return context to the handler entry point.

Two frame formats are supported:

- legacy frame for plain `sa_handler`
- extended frame for `SA_SIGINFO`

Both return through kernel-provided trampolines in the high user address space.

## Legacy Frame

The legacy frame is built from `struct sigframe` and `struct sigcontext`.

Stack layout after delivery:

```text
high addresses
  struct sigcontext
  int sig
  void *retaddr        -> SIG_TRAMPOLINE_ADDR
low addresses
```

Handler ABI:

```c
void handler(int sig);
```

`sys_sigreturn()` restores:

- general registers
- segment registers
- user `EIP`, `CS`, `EFLAGS`
- user `ESP`, `SS`
- saved signal mask

Security filtering rejects non-ring-3 code/data selectors and preserves kernel
control bits in `EFLAGS`.

## SA_SIGINFO Frame

The extended frame is built from `struct siginfo_frame`.

Stack layout after delivery:

```text
high addresses
  ucontext_t
  siginfo_t
  void *ucontext_ptr
  siginfo_t *info_ptr
  int sig
  void *retaddr        -> RT_SIG_TRAMPOLINE_ADDR
low addresses
```

Handler ABI:

```c
void handler(int sig, siginfo_t *info, void *ucontext);
```

`sys_rt_sigreturn()` restores the machine context from the embedded
`ucontext_t`, including the saved signal mask.

For synchronous trap delivery, the embedded `siginfo_t` carries:

- `si_signo`: delivered native signal number
- `si_code`: native Substrate trap code such as `SEGV_MAPERR`,
  `SEGV_ACCERR`, `FPE_INTDIV`, or `ILL_ILLOPC`
- `si_addr`: fault address for page faults or faulting `EIP` for instruction
  traps when available

## Alternate Signal Stack

If `SA_ONSTACK` is set and the thread has an enabled alternate signal stack,
delivery uses:

```text
ss_sp + ss_size
```

as the starting top-of-stack for frame construction.

The kernel sets `sig_on_stack` while the handler is active and clears it during
`sigreturn` / `rt_sigreturn`.

## Trampoline Contract

Two fixed trampoline entry points are exposed:

- `SIG_TRAMPOLINE_ADDR` for legacy handlers
- `RT_SIG_TRAMPOLINE_ADDR` for `SA_SIGINFO` handlers

The trampolines execute the matching `sigreturn` syscall so user handlers do
not need to issue those syscalls manually.

Compatibility personalities may use different frame builders and trampoline
contracts. In particular, the Linux personality emits Linux-compatible
`sigframe` and `rt_sigframe` layouts, translates Linux signal numbers and
sigsets at the syscall boundary, and may return through a Linux `sa_restorer`
callback when one is installed, while leaving the native Substrate signal
policy unchanged.

## Frame Validation

Before delivery, the kernel:

- validates the target user stack range
- aligns the frame to a 16-byte boundary
- copies the frame out with `copyout()`

If frame construction fails, the kernel terminates the process with `SIGSEGV`.
