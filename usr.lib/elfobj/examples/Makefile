include ../../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../../include -I../../../sys -I../src

EXAMPLES = create_minimal add_symbol_reloc merge_two inspect_symtab

.PHONY: all clean

all: $(EXAMPLES)

create_minimal: create_minimal.c ../libelfobj.a
	$(CC) $(CFLAGS) create_minimal.c ../libelfobj.a -o create_minimal

add_symbol_reloc: add_symbol_reloc.c ../libelfobj.a
	$(CC) $(CFLAGS) add_symbol_reloc.c ../libelfobj.a -o add_symbol_reloc

merge_two: merge_two.c ../libelfobj.a
	$(CC) $(CFLAGS) merge_two.c ../libelfobj.a -o merge_two

inspect_symtab: inspect_symtab.c ../libelfobj.a
	$(CC) $(CFLAGS) inspect_symtab.c ../libelfobj.a -o inspect_symtab

clean:
	rm -f $(EXAMPLES)
