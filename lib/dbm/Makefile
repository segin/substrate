include ../../Makefile.inc

CFLAGS    := $(CFLAGS) -I.
PICFLAGS  := $(SHLIB_CFLAGS) -I.
LDFLAGS   := $(USER_LDFLAGS)

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
SRCS = dbm.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

all: $(O)libdbm.a $(if $(NATIVE_BUILD),,$(O)libdbm.so.0)

$(O)libdbm.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(O)libdbm.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libdbm.so.0 \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libdbm.a $(O)libdbm.so $(O)libdbm.so.0

install: $(O)libdbm.a $(if $(NATIVE_BUILD),,$(O)libdbm.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)libdbm.a $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)libdbm.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libdbm.so.0 $(DESTDIR)/$(LIBDIR)/libdbm.so
endif
