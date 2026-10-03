include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
MATH_CFLAGS = $(CFLAGS) -I../../include -Isrc
PIC_MATH_CFLAGS = $(MATH_CFLAGS)
else
MATH_CFLAGS     = $(USER_CFLAGS) -fno-builtin -Isrc
PIC_MATH_CFLAGS = $(SHLIB_CFLAGS) -fno-builtin -Isrc
endif

SRCS = src/math_exp.c src/math_trig.c src/math_rem_pio2.c src/math_special.c \
       src/math_round.c src/math_wrap.c \
       src/mathf.c src/mathl.c src/fenv.c \
       src/cmath_trig.c src/cmath_explog.c src/cmath_hyper.c \
       src/cmath_special.c \
       src/math_obsolete.c src/math_narrowing.c \
       src/math_totalorder.c src/math_reentrant.c \
       src/cmath_log10.c
# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

all: $(O)libm.a $(if $(NATIVE_BUILD),,$(O)libm.so.0)

$(O)libm.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# The compiler's complex-arithmetic helpers (see below).  The 32-bit
# library takes them from libgcc_s.so.1, which the substrate toolchain
# installs in /lib.  There is no 64-bit libgcc_s on the system -- the
# 64-bit libraries are built with the host compiler -- so the 64-bit
# libm.so.0 links the few it needs from libgcc.a instead (they are
# hidden there, and stay private to libm).  A DT_NEEDED libgcc_s.so.1
# would make every 64-bit dynamic program fail to load, since libc.so.0
# needs libm.so.0.
ifeq ($(ARCH),x86_64)
LIBM_LIBGCC = $(LIBGCC)
else
LIBM_LIBGCC = -lgcc_s
endif

# libm has no DT_NEEDED — math is self-contained, no syscalls.
# No libm.so symlink in source tree — see lib/c/Makefile.
$(O)libm.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libm.so.0 -o $@ $(PIC_OBJS) \
	      $(LIBM_LIBGCC)
# DT_NEEDED libgcc_s.so.1: substrate's stage-2 gcc emits __divxc3 /
# __mulxc3 (long-double complex division/multiplication helpers) for
# some operations in cmath_special.c.  These live in libgcc_s.so.1;
# without the link-time reference, ld.so fails at runtime with
# "undefined symbol: __divxc3 in libm.so.0".
	@# Host ld stamps OSABI=SYSV (0); substrate's ld.so rejects with
	@# "file in wrong format".  Patch byte 7 to 0x40 (ELFOSABI_SUBSTRATE).
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(MATH_CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PIC_MATH_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libm.a $(O)libm.so $(O)libm.so.0

install: $(O)libm.a $(if $(NATIVE_BUILD),,$(O)libm.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)libm.a $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)libm.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libm.so.0 $(DESTDIR)/$(LIBDIR)/libm.so
endif
