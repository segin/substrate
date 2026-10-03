include ../../Makefile.inc

# libsys is the canonical typed system-call wrapper layer.  We
# build both libsys.a (for static-linked binaries) and libsys.so.0
# (for dynamic binaries — libc.so.0 declares DT_NEEDED libsys.so.0
# so any program that pulls in libc transitively gets libsys too).
#
# Objects and libraries land under $(O) (empty for i386, obj-x86_64/ for
# ARCH=x86_64) and install into /$(LIBDIR) and /usr/$(LIBDIR), so both
# architectures build from this directory side by side.

CFLAGS    := $(USER_CFLAGS) -fno-builtin -I.
PICFLAGS  := $(SHLIB_CFLAGS) -fno-builtin -I.
ASFLAGS   := $(USER_CFLAGS)
PICASFLAGS:= $(SHLIB_CFLAGS)

C_SRCS = proc.c sysctl.c sysinfo.c getpagesize.c mlock.c getpid.c \
         getuid.c pgrp.c getrusage.c getrandom.c times.c ioctl.c stime.c \
         ptrace.c reboot.c stat.c select.c chown.c
# vm86 mode and the LDT are i386 facilities.
ifneq ($(ARCH),x86_64)
C_SRCS += vm86.c modify_ldt.c
endif
# The raw syscall() entry is the architecture's system-call ABI.
ASM_SRCS = arch/$(ARCH)/syscall.S

OBJS     = $(C_SRCS:%.c=$(O)%.o)     $(ASM_SRCS:%.S=$(O)%.o)
PIC_OBJS = $(C_SRCS:%.c=$(O)%.pic.o) $(ASM_SRCS:%.S=$(O)%.pic.o)

all: $(O)libsys.a $(if $(NATIVE_BUILD),,$(O)libsys.so.0)

$(O)libsys.a: $(OBJS)
	$(AR) rcs $@ $^

# libsys has no DT_NEEDED — it's pure raw syscall surface and
# shouldn't pull libc in (libc depends on libsys, not the other way).
$(O)libsys.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libsys.so.0 -o $@ $(PIC_OBJS)
	@# Host ld stamps OSABI=SYSV (0); substrate's ld.so rejects with
	@# "file in wrong format".  Patch byte 7 to 0x40 (ELFOSABI_SUBSTRATE).
	@# Same scheme as lib/c/Makefile.
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(O)%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PICFLAGS) -c -o $@ $<

$(O)%.pic.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(PICASFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libsys.a $(O)libsys.so.0
ifeq ($(O),)
	rm -f *.o *.pic.o arch/*/*.o
endif

install: $(O)libsys.a $(if $(NATIVE_BUILD),,$(O)libsys.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)libsys.a $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)libsys.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libsys.so.0 $(DESTDIR)/$(LIBDIR)/libsys.so
endif
