PROVIDE(corpus_sym = 0x1234);
SECTIONS {
	.text : { KEEP(*(SORT_BY_NAME(.text.keep))) *(.text.drop) }
}
