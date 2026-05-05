# Substrate Linux Runner

`substrate-run` is a Linux-host compatibility runner for Substrate i386 ELF
binaries. It is intentionally separate from the Substrate target runtime
libraries.

The runner starts a traced 32-bit process from an anonymous executable copy of
the Substrate binary. The copy has only its ELF OSABI byte patched from
`ELFOSABI_SUBSTRATE` (`64`) to `ELFOSABI_SYSV` (`0`), which lets the Linux ELF
loader create the process without recursively invoking binfmt_misc. The traced
program still executes its original code and uses the Substrate native syscall
ABI.

Syscalls are intercepted with `PTRACE_SYSEMU`. The runner reads Substrate
syscall arguments from the user stack and either translates the request or
replays an equivalent Linux i386 syscall in the tracee. `fork` creates a real
Linux child and follows it with ptrace. `execve` preserves the PID by opening a
patched executable image in the tracee and invoking Linux `execveat`.

## Build

```sh
make -C linux/runner
```

The host kernel must support i386 compatibility execution and x86
`PTRACE_SYSEMU`.

## binfmt_misc

Install the runner and register handlers:

```sh
sudo make -C linux/runner install PREFIX=/usr/local
sudo /usr/local/libexec/substrate/register-binfmt.sh
```

Unregister handlers:

```sh
sudo /usr/local/libexec/substrate/unregister-binfmt.sh
```

The registration matches little-endian i386 ELF `ET_EXEC` and `ET_DYN` files
with `EI_OSABI == 64`.

## Current Scope

This is a compatibility runner, not a full Linux kernel personality. It covers
the common file, process, directory, time, memory, and exec syscalls needed by
simple static Substrate programs. Signal frame compatibility, native threading,
and dynamic loader edge cases remain future work.
