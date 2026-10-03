include ../../Makefile.inc

CFLAGS    := $(CFLAGS) -I.
PICFLAGS  := $(SHLIB_CFLAGS) -I.
LDFLAGS   := $(USER_LDFLAGS)

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
SRCS     = pwdb.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

all: $(O)libpwdb.a $(if $(NATIVE_BUILD),,$(O)libpwdb.so.0)

$(O)libpwdb.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(O)libpwdb.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libpwdb.so.0 \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libpwdb.a $(O)libpwdb.so $(O)libpwdb.so.0

install: $(O)libpwdb.a $(if $(NATIVE_BUILD),,$(O)libpwdb.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)libpwdb.a $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)libpwdb.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libpwdb.so.0 $(DESTDIR)/$(LIBDIR)/libpwdb.so
endif
