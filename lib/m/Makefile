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
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: libm.a $(if $(NATIVE_BUILD),,libm.so.0)

libm.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libm has no DT_NEEDED — math is self-contained, no syscalls.
# No libm.so symlink in source tree — see lib/c/Makefile.
libm.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libm.so.0 -o $@ $(PIC_OBJS) \
	      -lgcc_s
# DT_NEEDED libgcc_s.so.1: substrate's stage-2 gcc emits __divxc3 /
# __mulxc3 (long-double complex division/multiplication helpers) for
# some operations in cmath_special.c.  These live in libgcc_s.so.1;
# without the link-time reference, ld.so fails at runtime with
# "undefined symbol: __divxc3 in libm.so.0".
	@# Host ld stamps OSABI=SYSV (0); substrate's ld.so rejects with
	@# "file in wrong format".  Patch byte 7 to 0x40 (ELFOSABI_SUBSTRATE).
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

%.o: %.c
	$(CC) $(MATH_CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PIC_MATH_CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o src/*.pic.o libm.a libm.so libm.so.0

install: libm.a $(if $(NATIVE_BUILD),,libm.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 libm.a $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 libm.so.0 $(DESTDIR)/lib/
	@ln -sf libm.so.0 $(DESTDIR)/lib/libm.so
endif
