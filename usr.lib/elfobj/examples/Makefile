include ../../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../../include -I../../../sys -I../src

EXAMPLES = create_minimal add_symbol_reloc merge_two inspect_symtab

.PHONY: all clean

all: $(EXAMPLES)

%: %.c ../libelfobj.a
	$(CC) $(CFLAGS) $< ../libelfobj.a -o $@

clean:
	rm -f $(EXAMPLES)
