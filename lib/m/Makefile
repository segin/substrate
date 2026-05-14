include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
MATH_CFLAGS = $(CFLAGS) -I../../include
PIC_MATH_CFLAGS = $(MATH_CFLAGS)
else
MATH_CFLAGS     = $(USER_CFLAGS)  -fno-builtin
PIC_MATH_CFLAGS = $(SHLIB_CFLAGS) -fno-builtin
endif

SRCS = src/math_exp.c src/math_trig.c src/math_special.c \
       src/math_round.c src/math_wrap.c \
       src/mathf.c src/mathl.c src/fenv.c \
       src/cmath_trig.c src/cmath_explog.c src/cmath_hyper.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: libm.a $(if $(NATIVE_BUILD),,libm.so.0)

libm.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libm has no DT_NEEDED — math is self-contained, no syscalls.
# No libm.so symlink in source tree — see lib/c/Makefile.
libm.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libm.so.0 -o $@ $(PIC_OBJS)
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
