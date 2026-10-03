include ../../Makefile.inc

CFLAGS := $(CFLAGS) -I.
LDFLAGS := $(USER_LDFLAGS)

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
SRCS = main.c yyerror.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

all: $(O)liby.a $(if $(NATIVE_BUILD),,$(O)liby.so.0)

$(O)liby.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# liby has no DT_NEEDED — pure self-contained yacc runtime stubs.
$(O)liby.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,liby.so.0 -o $@ $(PIC_OBJS)

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SHLIB_CFLAGS) -I. -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)liby.a $(O)liby.so $(O)liby.so.0

install: $(O)liby.a $(if $(NATIVE_BUILD),,$(O)liby.so.0)
	mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	cp $(O)liby.a $(DESTDIR)/usr/$(LIBDIR)/liby.a
ifndef NATIVE_BUILD
	install -m 755 $(O)liby.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf liby.so.0 $(DESTDIR)/$(LIBDIR)/liby.so
endif

.PHONY: all clean install
