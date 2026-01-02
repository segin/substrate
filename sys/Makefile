include ../Makefile.inc

# Default to i386 if not specified
ARCH ?= i386

.PHONY: all clean install arch_kernel core kern drivers fs exec vfs exec_perso vm

all: kernel.bin
	@echo "Kernel build complete."

kernel.bin: arch_kernel kern drivers fs exec vfs exec_perso vm
	$(LD) -m elf_i386 -T arch/$(ARCH)/linker.ld -o kernel.bin \
		arch/$(ARCH)/kernel.o \
		kern/kern.o \
		drivers/video/video.o \
		drivers/serial/serial.o \
		drivers/input/input.o \
		drivers/storage/storage.o \
		vfs/vfs_subsys.o \
		fs/fs.o \
		fs/exec/exec.o \
		exec/perso/perso.o \
		vm/vm.o
	@echo "Linked kernel.bin"

arch_kernel:
	@echo "Building Kernel for $(ARCH)..."
	$(MAKE) -C arch/$(ARCH)

kern:
	@echo "Building Kernel Subsystems..."
	$(MAKE) -C kern

vm:
	@echo "Building VM Subsystem..."
	$(MAKE) -C vm

exec_perso:
	@echo "Building Personalities..."
	$(MAKE) -C exec/perso

drivers:
	@echo "Building Drivers..."
	$(MAKE) -C drivers/video
	$(MAKE) -C drivers/serial
	$(MAKE) -C drivers/input
	$(MAKE) -C drivers/storage

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
	$(MAKE) -C drivers/video clean
	$(MAKE) -C drivers/serial clean
	$(MAKE) -C drivers/input clean
	$(MAKE) -C drivers/storage clean
	$(MAKE) -C vfs clean
	$(MAKE) -C fs clean
	$(MAKE) -C fs/exec clean
	$(MAKE) -C exec/perso clean
	$(MAKE) -C vm clean
	rm -f kernel.bin

install:
	@echo "Installing Kernel..."
