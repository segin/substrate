include ../../Makefile.inc

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
# LIB and SHLIB stay bare names (soname, install link); the files
# themselves are $(O)$(LIB) and $(O)$(SHLIB).
LIB    = libuu.a
SHLIB  = libuu.so.0
SRCS   = uudecode.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

all: $(O)$(LIB) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))

$(O)$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(O)$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I. -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SHLIB_CFLAGS) -I. -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)$(LIB) $(O)$(SHLIB)

install: $(O)$(LIB) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)$(LIB) $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)$(SHLIB) $(DESTDIR)/$(LIBDIR)/
	@ln -sf $(SHLIB) $(DESTDIR)/$(LIBDIR)/libuu.so
endif
