ENTRY(efi_main)

SECTIONS
{
	. = 0x400000;
	.text :
	{
		*(.text)
		*(.text.*)
	}
	.rodata :
	{
		*(.rodata)
		*(.rodata.*)
	}
	.data :
	{
		*(.data)
		*(.data.*)
	}
	.bss :
	{
		*(.bss)
		*(COMMON)
	}
    
    _kernel_end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.property)
	}
}
