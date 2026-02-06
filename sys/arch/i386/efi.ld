ENTRY(efi_main)

SECTIONS
{
	/* EFI Loader ImageBase (Virtual Address in PE) */
	. = 0x400000;

	_setup_start = .;
	.setup : {
		*(.setup)
	}
	_setup_end = .;

	. = ALIGN(4096);

	/* High Half Kernel */
	/* We link at 0xC0000000 + Offset */
	/* Offset is relative to 0x400000 (ImageBase) */

	. = 0xC0000000 + (. - 0x400000);

	_kernel_start = .;

	.text : AT(ADDR(.text) - 0xC0000000 + 0x400000)
	{
		*(.text)
		*(.text.*)
	}

	.rodata : AT(ADDR(.rodata) - 0xC0000000 + 0x400000)
	{
		*(.rodata)
		*(.rodata.*)
	}

	.data : AT(ADDR(.data) - 0xC0000000 + 0x400000)
	{
		*(.data)
		*(.data.*)
		*(.got)
		*(.got.plt)
	}

	.bss : AT(ADDR(.bss) - 0xC0000000 + 0x400000)
	{
		*(COMMON)
		*(.bss)
	}
    
    _kernel_end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.property)
		*(.zimage_header)
		*(.multiboot)
	}
}
