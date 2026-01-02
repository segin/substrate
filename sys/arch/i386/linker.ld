ENTRY(_start)

SECTIONS
{
	/* The bootloader/QEMU jumps to the physical address. */
	. = 1M;
	_setup_phys_start = .;
	.setup : {
		_setup_start = .;
		*(.multiboot)
		*(.setup)
		_setup_end = .;
	}
	_setup_phys_end = .;

	/* The rest of the kernel lives at 3GB + 1MB */
	. = 0xC0100000 + (_setup_phys_end - _setup_phys_start);
	_kernel_start = .;

	.text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000)
	{
		*(.text)
	}

	.rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000)
	{
		*(.rodata)
	}

	.data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000)
	{
		*(.data)
	}

	.bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000)
	{
		*(COMMON)
		*(.bss)
	}

	_kernel_end = .;
}
