include ../../Makefile.inc

# We build TWO sets of objects from the same source tree:
#   *.o       — non-PIC, used to make libc.a (static archive)
#   *.pic.o   — PIC, used to make libc.so.0 (shared library)
# This costs an extra compile pass but keeps -fno-pie callers
# (every static-linked binary today) and the new ld.so users
# happy without runtime trade-offs.

CFLAGS    := $(USER_CFLAGS) -Iinclude -I../../include -I$(TOP)/sys
PICFLAGS  := $(SHLIB_CFLAGS) -Iinclude -I../../include -I$(TOP)/sys
LDFLAGS   := $(USER_LDFLAGS)
# libc.so.0 declares DT_NEEDED libm.so.0 (for feraiseexcept and
# friends) and DT_NEEDED libsys.so.0 (libsys is the canonical raw
# syscall surface; libc layers on top of it for the typed POSIX
# entry points and is migrating its ad-hoc int $0x80 stubs to call
# libsys's syscall(3) over time).  --unresolved-symbols=ignore-all
# allows the migration to land incrementally without breaking the
# link.
SOLDFLAGS := $(SHLIB_LDFLAGS) -Wl,-soname,libc.so.0 \
             -Wl,--unresolved-symbols=ignore-all \
             -L$(TOP)/lib/m   -l:libm.so.0 \
             -L$(TOP)/lib/sys -l:libsys.so.0

SRCS = src/sys.c src/stdlib.c src/string.c src/ctype.c src/assert.c src/dirent.c \
       src/pwd.c src/grp.c src/sha256.c src/md5.c \
       src/crypt.c src/crypt_sha256.c src/crypt_md5.c src/crypt_des.c \
       src/time/time.c src/div64.c fnmatch.c src/getopt.c src/wchar.c src/libgen.c \
       src/fpclassify.c src/socket_stubs.c src/glob.c \
       stdio/stdio_core.c stdio/stdstreams.c stdio/printf.c stdio/scanf.c sys_generic.c src/sysctl_helpers.c
ASM_SRCS = arch/i386/syscall.S arch/i386/crt0.S arch/i386/setjmp.S

OBJS    = $(SRCS:.c=.o) $(ASM_SRCS:.S=.o)
# Same source list as PIC objects, minus crt0 (executable startup,
# never a member of the shared library).
PIC_C_OBJS = $(SRCS:.c=.pic.o)
PIC_S_OBJS = $(filter-out arch/i386/crt0.pic.o,$(ASM_SRCS:.S=.pic.o))
PIC_OBJS   = $(PIC_C_OBJS) $(PIC_S_OBJS)

all: libc.a libc.so.0 crt0.o crti.o crtn.o

# Static archive (no crt0).
libc.a: $(OBJS)
	$(AR) rcs $@ $(filter-out arch/i386/crt0.o, $(OBJS))

# Shared library.  No DT_NEEDED — libc is the bottom of the
# dependency stack and resolves all its own external symbols
# via syscalls (asm stubs) or built-ins.  We deliberately do NOT
# create a `libc.so` symlink in the source tree because `-lc`
# would prefer it over `libc.a` and pull undefined symbols
# (e.g. feraiseexcept) into static-linked binaries.  The symlink
# only exists on the installed root under $(DESTDIR)/lib.
libc.so.0: $(PIC_OBJS)
	$(CC) $(SOLDFLAGS) -o $@ $(PIC_OBJS)
	@# Host ld produces OSABI=0 (SysV); substrate-ld rejects it as
	@# "file in wrong format" when later cross-builds try to link
	@# against this libc.  Patch the e_ident[EI_OSABI] byte to 0x40
	@# (ELFOSABI_SUBSTRATE).  Cheaper than re-invoking substrate-ld
	@# with a hand-built link line, and yields identical bytes
	@# everywhere except offset 7.
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

crt0.o: arch/i386/crt0.o
	cp $< $@

crti.o: arch/i386/crti.o
	cp $< $@

crtn.o: arch/i386/crtn.o
	cp $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PICFLAGS) -c $< -o $@

%.pic.o: %.S
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) libc.a libc.so libc.so.0 \
	      crt0.o crti.o crtn.o \
	      arch/i386/crti.o arch/i386/crtn.o

install: libc.a libc.so.0 crt0.o crti.o crtn.o
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 libc.a $(DESTDIR)/usr/lib/
	install -m 755 libc.so.0 $(DESTDIR)/lib/
	@ln -sf libc.so.0 $(DESTDIR)/lib/libc.so
	install -m 644 crt0.o $(DESTDIR)/usr/lib/
	install -m 644 crti.o $(DESTDIR)/usr/lib/
	install -m 644 crtn.o $(DESTDIR)/usr/lib/
