1. **Analyze:** The code in `sys/drivers/video/bios_menu.c` uses `vm86_bios_call(0x10, &regs)` to print strings character by character in `bios_puts`. Each character requires a context switch to VM86 mode, which is expensive.
2. **Optimize:** Modify `bios_puts` to copy the entire string to a low-memory buffer (e.g., `0x3000`) and use BIOS function INT 10h, AH=13h (Write String) to print the whole string in a single VM86 call (actually two, to get the cursor position).
3. **Implementation Details:**
   - Define `#define BIOS_STRING_BUFFER 0x3000` inside `sys/drivers/video/bios_menu.c`.
   - Update `bios_puts(const char *s)` to:
     - Expand `\n` to `\r\n` and place the modified string into `(char *)BIOS_STRING_BUFFER` (up to a reasonable limit, say 512 bytes, if longer we chunk it).
     - Query the current cursor position: `vm86_bios_call` with `AX=0x0300`, `BH=0`. The row/col is returned in `DX`.
     - Print the string: `vm86_bios_call` with `AX=0x1301`, `BX=0x0007`, `CX=length`, `DX=cursor_pos_from_above`, `ES=(0x3000 >> 4)`, `BP=(0x3000 & 0xF)`.
4. **Pre-commit:** Run the `pre_commit_instructions` tool to verify before committing.
