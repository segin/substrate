/*
 * sys/boot/stage2.ld - Linker script for Substrate stage2 bootloader
 *
 * Stage2 is loaded at physical 0x5000 by stage1 and runs in 32-bit
 * protected mode with a flat memory model (no paging).
 */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(stage2_main)

SECTIONS
{
    . = 0x5000;

    .text : {
        *(.text.entry)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
}
