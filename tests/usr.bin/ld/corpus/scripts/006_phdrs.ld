PHDRS {
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}
SECTIONS {
	.text : { *(.text) } :text
	.data : { *(.data) } :data
}
