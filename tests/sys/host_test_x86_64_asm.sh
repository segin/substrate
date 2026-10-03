#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT INT TERM

cc -m64 -I"$repo_root/sys" -D__ASSEMBLER__ -DSUBSTRATE_ARCH_X86_64 \
    -c "$repo_root/sys/arch/x86_64/boot/boot.S" -o "$workdir/boot.o"
cc -m64 -I"$repo_root/sys" -D__ASSEMBLER__ -DSUBSTRATE_ARCH_X86_64 \
    -c "$repo_root/sys/arch/x86_64/isr.S" -o "$workdir/isr.o"

nm "$workdir/boot.o" > "$workdir/boot.nm"
nm "$workdir/isr.o" > "$workdir/isr.nm"

grep -q ' T _start$' "$workdir/boot.nm"
grep -q ' t long_mode_entry$' "$workdir/boot.nm"
grep -q ' t higher_half$' "$workdir/boot.nm"

# Entry stubs: exceptions, the 8259 IRQs, the i386 system-call gate, IPIs.
grep -q ' T isr0$' "$workdir/isr.nm"
grep -q ' T isr14$' "$workdir/isr.nm"
grep -q ' T isr32$' "$workdir/isr.nm"
grep -q ' T isr47$' "$workdir/isr.nm"
grep -q ' T isr128$' "$workdir/isr.nm"
grep -q ' T isr253$' "$workdir/isr.nm"
grep -q ' T isr254$' "$workdir/isr.nm"
grep -q ' R msi_isr_stubs$' "$workdir/isr.nm"
grep -q ' T isr_exit$' "$workdir/isr.nm"

# Context switch, first runs and user entry.
grep -q ' T switch_stacks$' "$workdir/isr.nm"
grep -q ' T fork_child_return$' "$workdir/isr.nm"
grep -q ' T new_kernel_thread_trampoline$' "$workdir/isr.nm"
grep -q ' T new_user_thread_trampoline$' "$workdir/isr.nm"
grep -q ' T jump_to_userspace$' "$workdir/isr.nm"
grep -q ' T jump_to_elks$' "$workdir/isr.nm"

echo 'host_test_x86_64_asm: PASS'
