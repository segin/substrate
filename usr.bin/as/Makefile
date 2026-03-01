include ../../Makefile.inc

PROG = as
SRCS = as.c as_lexer.c as_parser.c as_symtab.c as_sections.c as_data.c as_elf_emit.c as_relax.c as_x86_encode.c as_x86_vex.c as_x86_evex.c as_x86_reloc.c
LDADD += ../../usr.lib/elfobj/libelfobj.a
USER_CFLAGS += -iquote ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

$(PROG): ../../usr.lib/elfobj/libelfobj.a

../../usr.lib/elfobj/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

all: alias_links

install: install_aliases

alias_links: $(PROG)
	ln -sf $(PROG) as.x86
	ln -sf $(PROG) as.x64

install_aliases: $(PROG)
	mkdir -p $(BINDIR)
	ln -sf as $(BINDIR)/as.x86
	ln -sf as $(BINDIR)/as.x64
