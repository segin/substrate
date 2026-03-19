#
# sys/boot/Makefile - Build the Substrate ext2 bootloader
#
# Produces:
#   stage1.bin  - 1024-byte boot block (stage1 + stage1.5, NASM)
#   stage2.bin  - Flat binary stage2 (C, linked at 0x5000)
#

NASM    ?= nasm
CC      ?= gcc
LD      ?= ld
OBJCOPY ?= objcopy

CFLAGS  = -m32 -ffreestanding -fno-pic -fno-pie -fno-builtin \
          -nostdlib -nostdinc -fno-stack-protector -Os -Wall -Wextra -Werror \
          -Wno-unused-parameter -std=gnu99

LDFLAGS = -m elf_i386 -T stage2.ld --oformat elf32-i386

STAGE2_OBJS = stage2.o

.PHONY: all clean

all: stage1.bin stage2.bin

stage1.bin: stage1.asm
	$(NASM) -f bin -o $@ $<

stage2.o: stage2.c stage2.h
	$(CC) $(CFLAGS) -c -o $@ $<

stage2.elf: $(STAGE2_OBJS) stage2.ld
	$(LD) $(LDFLAGS) -o $@ $(STAGE2_OBJS)

stage2.bin: stage2.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin
