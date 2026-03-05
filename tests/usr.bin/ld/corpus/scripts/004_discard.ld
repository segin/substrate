SECTIONS {
	.text : { *(.text) }
	/DISCARD/ : { *(.discard_me) }
}
