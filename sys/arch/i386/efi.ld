ENTRY(efi_main)

SECTIONS
{
	. = 0x400000;
	_setup_start = .;
	_setup_end = .;
	.text :
	{
		_kernel_start = .;
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
