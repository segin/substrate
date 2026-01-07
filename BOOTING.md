# Booting Substrate

Substrate supports four different boot methods on i386, each producing a specific kernel image.

## 1. Multiboot (`kernel.multiboot`)
Standard Multiboot 1 compliant ELF image. Bootable by GRUB, SeaBIOS, or QEMU `-kernel`.

```bash
qemu-system-i386 -kernel sys/kernel.multiboot
```

## 2. FreeBSD Loader (`kernel.freebsd`)
ELF image compatible with the FreeBSD `loader`. The kernel detects the FreeBSD bootinfo structures and magic values.

```bash
# In FreeBSD loader:
load /boot/kernel.freebsd
boot
```

## 3. EFI (`kernel.efi`)
PE32 binary for UEFI firmware. Bootable directly from an EFI shell or by a UEFI-compatible manager.

```bash
# In EFI Shell:
fs0:
kernel.efi
```

## 4. zImage / Raw (`kernel.zimage`)
Flat binary with a minimal Linux-compatible boot header. Can be loaded by legacy loaders (LILO, Syslinux) or written to a raw device.

```bash
# Using QEMU to boot raw binary (requires loading at 1MB)
qemu-system-i386 -device loader,file=sys/kernel.zimage,addr=0x100000
```

## 5. Booting with QEMU (Direct)

The fastest way to boot the kernel for testing is using QEMU's direct kernel loading feature.

### Using the Makefile
A helper target is provided in the root `Makefile`:

```bash
make debug
```

This executes:
```bash
qemu-system-i386 -kernel sys/kernel.bin -nographic -serial file:serial.log
```

### Manual Command
You can run QEMU manually with more options:

```bash
qemu-system-i386 -kernel sys/kernel.bin -m 128M -serial stdio
```

- `-kernel sys/kernel.bin`: Tells QEMU to load the kernel file as a Multiboot kernel.
- `-m 128M`: Allocates 128MB of RAM (the current kernel expects at least this much for its dummy PMM initialization).
- `-serial stdio`: Redirects the kernel's serial output to your terminal.

## 2. Booting with GRUB

To boot on real hardware or via a disk image in QEMU, you can use GRUB.

### GRUB Configuration (`grub.cfg`)
Create a `grub.cfg` file:

```text
menuentry "Substrate" {
    multiboot /boot/kernel.bin
    boot
}
```

### Creating an ISO Image
You can use `grub-mkrescue` to create a bootable ISO image:

1. Create a directory structure:
   ```bash
   mkdir -p isodir/boot/grub
   ```
2. Copy the kernel and config:
   ```bash
   cp sys/kernel.bin isodir/boot/
   cp grub.cfg isodir/boot/grub/
   ```
3. Generate the ISO:
   ```bash
   grub-mkrescue -o substrate.iso isodir
   ```

## 3. Kernel Command Line
The kernel supports an `init=` parameter to specify the initial process:

```bash
qemu-system-i386 -kernel sys/kernel.bin -append "init=/bin/sh"
```

## 4. Troubleshooting
- **Invalid Multiboot Magic:** If QEMU or GRUB complains about the Multiboot header, ensure that the kernel was linked correctly and that `sys/arch/i386/boot.S` (which contains the header) is the first object file linked or is placed correctly by the linker script.
- **Panic on Boot:** Check the serial output or the VGA console (if not using `-nographic`) for panic messages. Common causes include insufficient memory or missing init binaries.
