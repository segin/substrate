#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT INT TERM

cc -m64 -c "$repo_root/sys/arch/x86_64/boot/boot.S" -o "$workdir/boot.o"
cc -m64 -c "$repo_root/sys/arch/x86_64/isr.S" -o "$workdir/isr.o"
cc -m64 -c "$repo_root/sys/arch/x86_64/switch.S" -o "$workdir/switch.o"

nm "$workdir/boot.o" > "$workdir/boot.nm"
nm "$workdir/isr.o" > "$workdir/isr.nm"
nm "$workdir/switch.o" > "$workdir/switch.nm"

grep -q ' T _start$' "$workdir/boot.nm"
grep -q ' t long_mode_entry$' "$workdir/boot.nm"
grep -q ' t higher_half$' "$workdir/boot.nm"

grep -q ' T isr0$' "$workdir/isr.nm"
grep -q ' T irq0$' "$workdir/isr.nm"
grep -q ' T isr128$' "$workdir/isr.nm"
grep -q ' T swapgs_if_needed$' "$workdir/isr.nm"

grep -q ' T switch_to$' "$workdir/switch.nm"
grep -q ' T switch_to_first$' "$workdir/switch.nm"
grep -q ' T context_init$' "$workdir/switch.nm"
grep -q ' T fork_return$' "$workdir/switch.nm"

echo 'host_test_x86_64_asm: PASS'
