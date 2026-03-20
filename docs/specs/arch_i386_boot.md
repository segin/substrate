# i386 Boot Architecture

## BIOS Floppy Boot Artifact (`sys/kernel.flp`)
- Fixed-layout 1.44MB image.
- Two-stage real-mode loader.
- Prompts for a hand-typed kernel command line.
- Falls back to a built-in default boot line on empty prompt.
- Refuses to boot on pre-386 CPUs.
- Loads `kernel.zimage` from the floppy payload.
- Patches the Linux boot header `cmd_line_ptr`.
- Transfers control to the normal `zImage` setup entry.

## i386 `zImage` Setup Path
- Fabricates Multiboot memory information from BIOS services in descending fidelity order: `E820`, then legacy aggregate sizing via `E801`, then `INT 15h AH=88h`.
- If only aggregate sizing is available, the kernel reserves the first 1MB conservatively and seeds PMM from one extended-memory run above 1MB.
- Owns BIOS text-mode programming before protected-mode handoff: `vga=ask` prompts on the BIOS text console and applies the selected BIOS text mode.
- Explicit `textmode=` or `video=text:COLSxROWS` requests are applied directly without the menu.
- Appends canonical handoff tokens so the higher-half VT geometry stays aligned with the programmed mode after boot.
- The fixed menu includes `80x25`, `80x43`, and `80x50`.
- Offers VBE text modes if advertised by firmware: `132x25`, `132x43`, `132x50`, and `132x60`.
