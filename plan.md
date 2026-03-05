1. **Analyze:** The code in `sys/drivers/video/bios_menu.c` uses `vm86_bios_call(0x10, &regs)` to print strings character by character in `bios_puts`. Each character requires a context switch to VM86 mode, which is expensive.
2. **Optimize:** We can modify `bios_puts` to copy the entire string to a low-memory buffer (e.g., 0x3000, which is mentioned in the memory instructions as being reserved for this) and use BIOS function INT 10h, AH=13h (Write String) to print the whole string in a single VM86 call.
3. **Implementation details:**
   - Define `#define BIOS_STRING_BUFFER 0x3000` (from memory).
   - In `bios_puts(const char *s)`, calculate the length of the string while handling `\n` to `\r\n` expansion if needed.
   - Copy the string to `BIOS_STRING_BUFFER`. (Need to expand `\n` to `\r\n` during copy).
   - Call `vm86_bios_call` with `AH=13h` (Write string), `AL=01h` (update cursor), `BH=0` (page 0), `BL=07h` (Light grey), `CX=length`, `ES=BIOS_STRING_BUFFER >> 4`, `BP=BIOS_STRING_BUFFER & 0xF`.
   Wait, the cursor position is tricky with AH=13h. If `AL=01h`, it updates the cursor, but we don't specify the row/col. Let me check if AH=13h uses the current cursor position if we just let it. Ah, AH=13h requires DH=row, DL=col. That means we have to either get the current cursor position (AH=03h) and pass it, or we can use another method.
   Actually, `bios_putc` handles `\n` to `\r\n`. Does AH=13h handle `\r\n` correctly? Yes, it's just BIOS write string.
   Let's check INT 10h AH=13h documentation.
   Input:
   AH = 13h
   AL = write mode
      bit 0: update cursor after writing
      bit 1: string contains alternating characters and attributes
   BH = page number
   BL = attribute (if AL bit 1 = 0)
   CX = string length
   DH, DL = row, column at which to start writing
   ES:BP -> string to write

   If we need to pass DH, DL (row, col), we need to query them first with AH=03h.
   Query cursor position:
   AH = 03h
   BH = page number (0)
   Return:
   CH = start scan line
   CL = end scan line
   DH = row
   DL = column

   Let's write a quick implementation.
