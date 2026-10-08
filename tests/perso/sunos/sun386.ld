/*
 * sun386.ld - lay a program out as the Sun386i's linker does.
 *
 * The file's first page is mapped at 0x1000, headers and all, so the text
 * begins 0xd0 into it -- past a COFF file header, an optional header and
 * the section headers.  The data starts on the next page boundary after
 * the text and the bss follows it.  Page 0 is left unmapped.
 *
 * mkcoff.py puts the headers in front of the flat image this produces.
 */
ENTRY(_start)

SECTIONS
{
    . = 0x10d0;
    _text = .;
    /* No input section may ask for more alignment than 0x10d0 has, or
     * the text would start later than the headers end. */
    .text : SUBALIGN(16) {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }
    _etext = .;

    . = ALIGN(0x1000);
    _data = .;
    .data : {
        *(.data .data.*)
        /* The image must reach here for the file to hold all of it. */
        LONG(0)
    }
    _edata = .;

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    _end = .;

    /DISCARD/ : {
        *(.note*) *(.comment) *(.eh_frame*) *(.got*) *(.rel*)
    }
}
