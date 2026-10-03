# arch/x86_64/Makefile - the 64-bit kernel
#
# Invoked by `make -C sys ARCH=x86_64`.  Same source tree as the i386
# kernel; every object goes under obj/ here so the two builds never share
# an object file (the i386 build compiles in place).  Output:
#
#   sys/kernel-x86_64.elf   ELF64 with symbols; GRUB `multiboot2` loads it
#   sys/kernel-x86_64.bin   flat image for multiboot 1 loaders (QEMU
#                           -kernel, GRUB `multiboot`); installed as
#                           /vmunix64
#
# Machine-independent sources join MI_SRCS as they are made LP64-clean.

SYS     := $(abspath ../..)
TOP     := $(abspath $(SYS)/..)
OBJDIR  := obj
KERNEL  := $(SYS)/kernel-x86_64

CC      ?= cc
LD      ?= ld
OBJCOPY ?= objcopy
GCCINC  := $(shell $(CC) -print-file-name=include)

CFLAGS64 := -m64 -march=x86-64 -mcmodel=kernel -mno-red-zone \
	-mgeneral-regs-only -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pie -fno-pic -fno-asynchronous-unwind-tables \
	-fno-omit-frame-pointer -ffunction-sections -fdata-sections \
	-std=c2x -Wall -Wextra -Werror -g -nostdinc \
	-I$(SYS)/include -I$(TOP)/include -I$(SYS) -isystem $(GCCINC) \
	-MMD -MP
ASFLAGS64 := -m64 -D__ASSEMBLER__ -nostdinc -I$(SYS)/include -I$(SYS) -MMD -MP
LDFLAGS64 := -m elf_x86_64 -nostdlib -z max-page-size=0x1000 \
	--gc-sections -T linker.ld

ARCH_SRCS := boot/boot.S isr.S kmain.c cons.c trap.c gdt.c idt.c
MI_SRCS   := lib/string.c

OBJS := $(patsubst %.S,$(OBJDIR)/%.o,$(patsubst %.c,$(OBJDIR)/%.o,$(ARCH_SRCS))) \
	$(patsubst %.c,$(OBJDIR)/mi/%.o,$(MI_SRCS))

.PHONY: all clean

all: $(KERNEL).elf $(KERNEL).bin

$(KERNEL).elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS64) -o $@ $(OBJS)

$(KERNEL).bin: $(KERNEL).elf
	$(OBJCOPY) -O binary $< $@
	@echo "Linked $(notdir $(KERNEL)).elf and $(notdir $(KERNEL)).bin"

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS64) -c $< -o $@

$(OBJDIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS64) -c $< -o $@

$(OBJDIR)/mi/%.o: $(SYS)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS64) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(KERNEL).elf $(KERNEL).bin

-include $(OBJS:.o=.d)
