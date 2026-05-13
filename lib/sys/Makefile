include ../../Makefile.inc

# libsys is the canonical typed system-call wrapper layer.  We
# build both libsys.a (for static-linked binaries) and libsys.so.0
# (for dynamic binaries — libc.so.0 declares DT_NEEDED libsys.so.0
# so any program that pulls in libc transitively gets libsys too).

CFLAGS    := $(USER_CFLAGS) -fno-builtin
PICFLAGS  := $(SHLIB_CFLAGS) -fno-builtin
ASFLAGS   := $(USER_CFLAGS)
PICASFLAGS:= $(SHLIB_CFLAGS)

C_SRCS = vm86.c proc.c sysctl.c sysinfo.c getpagesize.c mlock.c getpid.c \
         getuid.c pgrp.c getrusage.c getrandom.c times.c ioctl.c stime.c \
         ptrace.c reboot.c modify_ldt.c stat.c select.c chown.c
ASM_SRCS = syscall.S

OBJS     = $(C_SRCS:.c=.o)     $(ASM_SRCS:.S=.o)
PIC_OBJS = $(C_SRCS:.c=.pic.o) $(ASM_SRCS:.S=.pic.o)

all: libsys.a $(if $(NATIVE_BUILD),,libsys.so.0)

libsys.a: $(OBJS)
	$(AR) rcs $@ $^

# libsys has no DT_NEEDED — it's pure raw syscall surface and
# shouldn't pull libc in (libc depends on libsys, not the other way).
libsys.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libsys.so.0 -o $@ $(PIC_OBJS)
	@# Host ld stamps OSABI=SYSV (0); substrate's ld.so rejects with
	@# "file in wrong format".  Patch byte 7 to 0x40 (ELFOSABI_SUBSTRATE).
	@# Same scheme as lib/c/Makefile.
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(ASFLAGS) -c -o $@ $<

%.pic.o: %.c
	$(CC) $(PICFLAGS) -c -o $@ $<

%.pic.o: %.S
	$(CC) $(PICASFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.pic.o libsys.a libsys.so.0

install: libsys.a $(if $(NATIVE_BUILD),,libsys.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 libsys.a $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 libsys.so.0 $(DESTDIR)/lib/
	@ln -sf libsys.so.0 $(DESTDIR)/lib/libsys.so
endif
