include ../../Makefile.inc

# Thin POSIX wrappers around /sbin/ld.so's __ldso_dl* exports.
# Builds both libdl.a (for static-linked binaries that won't ever
# call into ld.so) and libdl.so.0 (for dynamic binaries).

CFLAGS    := $(USER_CFLAGS) -I../../include
PICFLAGS  := $(SHLIB_CFLAGS) -I../../include

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
SRCS = dl.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

.PHONY: all clean install

all: $(O)libdl.a $(if $(NATIVE_BUILD),,$(O)libdl.so.0)

$(O)libdl.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libdl has no DT_NEEDED — the __ldso_dl* references are weak and
# resolve at runtime against /sbin/ld.so itself (which is always
# loaded for any dynamic binary that pulled libdl in).
$(O)libdl.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libdl.so.0 \
	      -Wl,--unresolved-symbols=ignore-all \
	      -o $@ $(PIC_OBJS)
	@# Host ld produces OSABI=0 (SysV); substrate-cross-ld rejects
	@# it as "file in wrong format" when later cross-builds try to
	@# link against this libdl.  Patch e_ident[EI_OSABI] to 0x40
	@# (ELFOSABI_SUBSTRATE).  Same recipe as lib/c, lib/m, lib/sys.
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libdl.a $(O)libdl.so.0

install: $(O)libdl.a $(if $(NATIVE_BUILD),,$(O)libdl.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)libdl.a $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)libdl.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libdl.so.0 $(DESTDIR)/$(LIBDIR)/libdl.so
endif
