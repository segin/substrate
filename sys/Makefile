include ../Makefile.inc

# Default to i386 if not specified
ARCH ?= i386
LIBGCC := $(shell $(CC) -m32 -print-libgcc-file-name)

.PHONY: all clean install arch_kernel core kern pm drivers fs exec vfs exec_perso vm multiboot freebsd zimage efi

all: kernel.bin kernel.efi kernel.multiboot kernel.freebsd kernel.zimage
	@echo "Kernel build complete (all formats)."

kernel.bin: arch_kernel kern pm drivers fs exec vfs exec_perso vm
	$(LD) -m elf_i386 -T arch/$(ARCH)/linker.ld -o kernel.bin \
		arch/$(ARCH)/kernel.o \
		kern/kern.o \
		pm/pm.o \
		drivers/video/video.o \
		drivers/serial/serial.o \
		drivers/input/input.o \
		drivers/storage/storage.o \
		drivers/devices/devices.o \
		drivers/virtio/virtio_drv.o \
		vfs/vfs_subsys.o \
		fs/fs.o \
		fs/exec/exec.o \
		exec/perso/perso.o \
		vm/vm.o \
		$(LIBGCC)
	@echo "Linked kernel.bin"

kernel.multiboot: kernel.bin
	cp kernel.bin kernel.multiboot

kernel.freebsd: kernel.bin
	cp kernel.bin kernel.freebsd

kernel.zimage: kernel.bin
	# For zImage, we often want a flat binary if using it with legacy loaders
	objcopy -O binary kernel.bin kernel.zimage

kernel.efi: arch_kernel kern drivers fs exec vfs exec_perso vm
	$(LD) -m elf_i386 -T arch/$(ARCH)/efi.ld -o kernel_efi.elf \
		arch/$(ARCH)/kernel.o \
		kern/kern.o \
		pm/pm.o \
		drivers/video/video.o \
		drivers/serial/serial.o \
		drivers/input/input.o \
		drivers/storage/storage.o \
		drivers/devices/devices.o \
		drivers/virtio/virtio_drv.o \
		vfs/vfs_subsys.o \
		fs/fs.o \
		fs/exec/exec.o \
		exec/perso/perso.o \
		vm/vm.o \
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

drivers:
	@echo "Building Drivers..."
	$(MAKE) -C drivers/video
	$(MAKE) -C drivers/serial
	$(MAKE) -C drivers/input
	$(MAKE) -C drivers/storage
	$(MAKE) -C drivers/devices
	$(MAKE) -C drivers/virtio

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
	$(MAKE) -C fs/exec

clean:
	@echo "Cleaning Kernel..."
	$(MAKE) -C arch/$(ARCH) clean
	$(MAKE) -C kern clean
	$(MAKE) -C pm clean
	$(MAKE) -C drivers/video clean
	$(MAKE) -C drivers/serial clean
	$(MAKE) -C drivers/input clean
	$(MAKE) -C drivers/storage clean
	$(MAKE) -C drivers/devices clean
	$(MAKE) -C drivers/virtio clean
	$(MAKE) -C vfs clean
	$(MAKE) -C fs clean
	$(MAKE) -C fs/exec clean
	$(MAKE) -C exec/perso clean
	$(MAKE) -C vm clean
	rm -f kernel.bin

install:
	@echo "Installing Kernel..."
