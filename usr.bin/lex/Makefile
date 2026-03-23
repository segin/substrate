include ../../Makefile.inc

PROG=   lex
SRCS=   main.c options.c parser.c symtab.c regex.c dfa.c codegen.c
OBJS=   $(SRCS:.c=.o)
CFLAGS_LOCAL = -I.
BINDIR= /usr/bin

all: $(PROG)

$(PROG): $(OBJS)
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) -o $@ $(OBJS)
else
	$(CC) $(USER_LDFLAGS) -o $@ $(OBJS) $(TOP)/lib/c/crt0.o -L$(TOP)/lib/c -lc $(LIBGCC)
	$(call BRAND_SUBSTRATE,$@)
endif

%.o: %.c
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) $(CFLAGS_LOCAL) -idirafter ../../include -c $< -o $@
else
	$(CC) $(USER_CFLAGS) $(CFLAGS_LOCAL) -c $< -o $@
endif

clean:
	rm -f $(PROG) $(OBJS)

install: $(PROG)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)

.PHONY: all clean install
