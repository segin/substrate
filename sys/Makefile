include ../Makefile.inc

# Default to i386 if not specified
ARCH ?= i386
# Note: No libgcc needed - we implement our own 64-bit helpers in kern/lib.c

.PHONY: all clean install arch_kernel core kern pm drivers fs exec vfs exec_perso vm multiboot freebsd zimage efi tests klib

all: kernel.bin kernel.efi kernel.multiboot kernel.freebsd kernel.zimage
	@echo "Kernel build complete (all formats)."

kernel.bin: arch_kernel klib kern pm drivers fs exec vfs exec_perso vm tests
	$(LD) -m elf_i386 -T arch/$(ARCH)/linker.ld -o kernel.bin \
		arch/$(ARCH)/kernel.o \
		lib/lib.o \
		kern/kern.o \
		pm/pm.o \
		drivers/video/video.o \
		drivers/input/input.o \
		drivers/console/console_subsys.o \
		drivers/console/uart/uart_drv.o \
		drivers/storage/storage.o \
		drivers/devices/devices.o \
		drivers/virtio/virtio_drv.o \
		vfs/vfs_subsys.o \
		fs/fs.o \
		exec/formats/exec.o \
		exec/perso/perso.o \
		vm/vm.o \
	../tests/sys/tests.o
	@echo "Linked kernel.bin"

kernel.multiboot: kernel.bin
	cp kernel.bin kernel.multiboot

kernel.freebsd: kernel.bin
	cp kernel.bin kernel.freebsd

kernel.zimage: kernel.bin
	@echo "Building zImage..."
	$(MAKE) -C arch/$(ARCH) zimage_header.bin
	objcopy -O binary kernel.bin kernel.raw
	arch/$(ARCH)/mkzimage.py arch/$(ARCH)/zimage_header.bin kernel.raw kernel.zimage
	rm -f kernel.raw

kernel.efi: arch_kernel klib kern drivers fs exec vfs exec_perso vm tests
	$(LD) -m elf_i386 -T arch/$(ARCH)/efi.ld -o kernel_efi.elf \
		arch/$(ARCH)/kernel.o \
		lib/lib.o \
		kern/kern.o \
		pm/pm.o \
		drivers/video/video.o \
		drivers/input/input.o \
		drivers/console/console_subsys.o \
		drivers/console/uart/uart_drv.o \
		drivers/storage/storage.o \
		drivers/devices/devices.o \
		drivers/virtio/virtio_drv.o \
		vfs/vfs_subsys.o \
		fs/fs.o \
		exec/formats/exec.o \
		exec/perso/perso.o \
		vm/vm.o \
	../tests/sys/tests.o \
		$(LIBGCC)
	objcopy -O pe-i386 kernel_efi.elf kernel.efi
	@echo "Linked kernel.efi"

arch_kernel:
	@echo "Building Kernel for $(ARCH)..."
	$(MAKE) -C arch/$(ARCH)

kern:
	@echo "Building Kernel Subsystems..."
	$(MAKE) -C kern

vm:
	@echo "Building VM Subsystem..."
	$(MAKE) -C vm

pm:
	@echo "Building Process Manager..."
	$(MAKE) -C pm

exec_perso:
	@echo "Building Personalities..."
	$(MAKE) -C exec/perso

klib:
	@echo "Building Kernel Libraries..."
	$(MAKE) -C lib

	@echo "Building Drivers..."
	$(MAKE) -C drivers/video
	$(MAKE) -C drivers/input
	$(MAKE) -C drivers/storage
	$(MAKE) -C drivers/devices
	$(MAKE) -C drivers/virtio
	$(MAKE) -C drivers/console
	$(MAKE) -C drivers/console/uart


libs:
	@echo "Building User Libraries..."
	$(MAKE) -C ../lib/c
	$(MAKE) -C ../lib/pthreads

vfs:
	@echo "Building VFS..."
	$(MAKE) -C vfs

fs:
	@echo "Building FS..."
	$(MAKE) -C fs

exec:
	@echo "Building Exec Loaders..."
	$(MAKE) -C exec/formats

clean:
	@echo "Cleaning Kernel..."
	$(MAKE) -C arch/$(ARCH) clean
	$(MAKE) -C kern clean
	$(MAKE) -C pm clean
	$(MAKE) -C drivers/video clean
	$(MAKE) -C drivers/input clean
	$(MAKE) -C drivers/storage clean
	$(MAKE) -C drivers/devices clean
	$(MAKE) -C drivers/virtio clean
	$(MAKE) -C drivers/console clean
	$(MAKE) -C drivers/console/uart clean
	$(MAKE) -C vfs clean
	$(MAKE) -C fs clean
	$(MAKE) -C exec/formats clean
	$(MAKE) -C exec/perso clean
	$(MAKE) -C vm clean
	$(MAKE) -C lib clean
	rm -f kernel.bin kernel.zimage

tests:
	$(MAKE) -C ../tests/sys

install: kernel.bin
	@echo "Installing Kernel to $(DESTDIR)/vmunix..."
	@mkdir -p $(DESTDIR)
	@cp kernel.bin $(DESTDIR)/vmunix
