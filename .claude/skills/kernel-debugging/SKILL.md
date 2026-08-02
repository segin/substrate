---
name: kernel-debugging
description: Debug Substrate kernel crashes, hangs, and triple faults with QEMU + GDB — setup, single-stepping, exception trapping, and debugging principles. Use whenever the kernel crashes, triple-faults, hangs, or misbehaves at boot.
---

# Kernel Debugging

When debugging kernel crashes (including triple faults):

1. **QEMU with GDB:**
   ```bash
   # Terminal 1: Start QEMU with debugging, -no-reboot stops on triple fault
   qemu-system-i386 -kernel sys/kernel.bin -no-reboot -s -S

   # Terminal 2: Connect GDB
   gdb -ex "file sys/kernel.bin" -ex "target remote :1234"
   ```

2. **Single-Step Debugging:**
   - Use `si` (step instruction) one at a time in gdb
   - Use `break <function>` to set breakpoints
   - Use `info registers` to check CPU state
   - When QEMU hits triple fault with `-no-reboot`, it halts and gdb shows connection closed

3. **Exception Trapping:**
   - Set breakpoints on IDT handlers: `break isr_common_stub`, `break double_fault_handler`
   - Use QEMU monitor (`Ctrl+Alt+2`) for low-level CPU inspection

4. **Debugging Principles:**
   - **Never recreate code** - always restore from git history when reverting changes
   - **Single-step from crash point** - triple faults don't return to gdb, so step one instruction at a time
   - **Check BSS/stack** - large static arrays can cause stack overflow or memory corruption

## Debugging Note
If the kernel hangs in `hlt`, check `eflags` bit 9. If `IF=1`, the IRQ may be masked at the PIC or the controller state is stuck.
