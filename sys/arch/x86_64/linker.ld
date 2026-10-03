/*
 * linker.ld - x86_64 kernel
 *
 * Linked at KERNEL_VMA + 1 MiB, loaded at 1 MiB physical.  Everything
 * from _kernel_start to _load_end is one contiguous image, which
 * `objcopy -O binary` turns into the flat file the multiboot 1 header's
 * a.out-kludge fields describe; .bss follows it and is zeroed by the
 * loader up to _bss_end.  Keep KERNEL_VMA in step with layout.h.
 */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS
{
	. = KERNEL_VMA + 0x100000;
	_kernel_start = .;

	.text : AT(ADDR(.text) - KERNEL_VMA) {
		KEEP(*(.multiboot))
		KEEP(*(.multiboot2))
		*(.text.boot)
		*(.text .text.*)
	}

	.rodata ALIGN(4096) : AT(ADDR(.rodata) - KERNEL_VMA) {
		*(.rodata .rodata.*)
	}

	.data ALIGN(4096) : AT(ADDR(.data) - KERNEL_VMA) {
		*(.data .data.*)
	}
	_load_end = .;

	/* The i386 kernel frees its boot-only code after boot
	 * (pmm_reclaim_setup); this one has none, so the range is empty. */
	_setup_start = .;
	_setup_end = .;

	.bss ALIGN(4096) (NOLOAD) : AT(ADDR(.bss) - KERNEL_VMA) {
		*(.bss.boot)
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(4096);
	}
	_bss_end = .;
	_kernel_end = .;

	/DISCARD/ : {
		*(.eh_frame*)
		*(.note*)
		*(.comment)
	}
}
