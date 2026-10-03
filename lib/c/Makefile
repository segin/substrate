include ../../Makefile.inc

# We build TWO sets of objects from the same source tree:
#   *.o       — non-PIC, used to make libc.a (static archive)
#   *.pic.o   — PIC, used to make libc.so.0 (shared library)
# This costs an extra compile pass but keeps -fno-pie callers
# (every static-linked binary today) and the new ld.so users
# happy without runtime trade-offs.

CFLAGS    := $(USER_CFLAGS) -Iinclude -I../../include -I$(TOP)/sys
PICFLAGS  := $(SHLIB_CFLAGS) -Iinclude -I../../include -I$(TOP)/sys

# Keep frame pointers in libc.  Debug guards in src/string.c walk the
# saved-ebp chain to produce a multi-frame user backtrace when memcpy
# / memset / strdup are called with junk args; that walk only works
# if gcc preserves the push %ebp / mov %esp, %ebp prologue.  Default
# -O0 already does, but make it explicit so a future opt-level bump
# doesn't silently disable it.
CFLAGS    += -fno-omit-frame-pointer
PICFLAGS  += -fno-omit-frame-pointer

# libc *defines* the functions gcc otherwise treats as builtins, so it must
# not be compiled with builtin semantics for them.  Two reasons, one cosmetic
# and one not:
#
#   - gcc knows the builtin memcpy/memset take nonnull pointers, so the
#     deliberate NULL guards in src/string.c are provably-dead code to it.
#     GCC 13 rejects them outright under -Werror=nonnull-compare (GCC 16 no
#     longer warns, which is why this only ever broke on CI); worse, being
#     "dead" the guards are the optimiser's to delete, so the hardening they
#     provide is not something we should be relying on by luck.
#   - a byte loop inside memcpy() is exactly the pattern gcc likes to
#     recognise and rewrite into a call to memcpy(), i.e. into infinite
#     recursion.  -O0 hides this today; it is a landmine at any other level.
#
# tests/Makefile already compiles src/string.c this way for the host unit
# tests; the library itself simply never got the same treatment.
CFLAGS    += -fno-builtin
PICFLAGS  += -fno-builtin

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
             -L$(TOP)/lib/m/$(O)   -l:libm.so.0 \
             -L$(TOP)/lib/sys/$(O) -l:libsys.so.0

SRCS = src/sys.c src/atomic.c src/stdlib.c src/string.c src/ctype.c src/assert.c src/dirent.c \
       src/pwd.c src/grp.c src/sha256.c src/md5.c \
       src/crypt.c src/crypt_sha256.c src/crypt_md5.c src/crypt_des.c \
       src/time/time.c src/div64.c fnmatch.c src/getopt.c src/wchar.c src/libgen.c \
       src/fpclassify.c src/socket.c src/glob.c src/wordexp.c src/inttypes.c \
       src/rand48.c src/random_r.c src/cvt.c src/hsearch_r.c src/tsearch.c src/ctermid.c src/netdb_r.c \
       src/shadow.c src/mntent.c src/getdate.c \
       src/posix_extra.c src/posix_extra2.c src/posix_extra3.c src/ifaddrs.c src/nl_types.c src/utmp.c \
       src/lfs64.c src/spawn.c \
       src/syslog.c src/err.c src/libintl.c src/tls.c src/tls_static.c src/dl_iterate_phdr.c \
       stdio/stdio_core.c stdio/stdstreams.c stdio/printf.c stdio/scanf.c src/sysctl_helpers.c
# Startup and system-call code is per architecture (arch/i386, arch/x86_64).
ASM_SRCS = arch/$(ARCH)/syscall.S arch/$(ARCH)/crt0.S arch/$(ARCH)/setjmp.S

# Objects and libraries land under $(O): empty for i386, which builds in
# place, and obj-x86_64/ for ARCH=x86_64, which installs into /lib64 and
# /usr/lib64 beside the i386 set (Makefile.inc).
OBJS    = $(SRCS:%.c=$(O)%.o) $(ASM_SRCS:%.S=$(O)%.o)
# Same source list as PIC objects, minus crt0 (executable startup,
# never a member of the shared library).
PIC_C_OBJS = $(SRCS:%.c=$(O)%.pic.o)
PIC_S_OBJS = $(filter-out $(O)arch/$(ARCH)/crt0.pic.o,$(ASM_SRCS:%.S=$(O)%.pic.o))
PIC_OBJS   = $(PIC_C_OBJS) $(PIC_S_OBJS)

all: $(O)libc.a $(O)libc.so.0 $(O)crt0.o $(O)crti.o $(O)crtn.o

# Static archive (no crt0).
$(O)libc.a: $(OBJS)
	$(AR) rcs $@ $(filter-out $(O)arch/$(ARCH)/crt0.o, $(OBJS))

# Shared library.  No DT_NEEDED — libc is the bottom of the
# dependency stack and resolves all its own external symbols
# via syscalls (asm stubs) or built-ins.  We deliberately do NOT
# create a `libc.so` symlink in the source tree because `-lc`
# would prefer it over `libc.a` and pull undefined symbols
# (e.g. feraiseexcept) into static-linked binaries.  The symlink
# only exists on the installed root under $(DESTDIR)/$(LIBDIR).
$(O)libc.so.0: $(PIC_OBJS)
	$(CC) $(SOLDFLAGS) -o $@ $(PIC_OBJS)
	@# Host ld produces OSABI=0 (SysV); substrate-ld rejects it as
	@# "file in wrong format" when later cross-builds try to link
	@# against this libc.  Patch the e_ident[EI_OSABI] byte to 0x40
	@# (ELFOSABI_SUBSTRATE).  Cheaper than re-invoking substrate-ld
	@# with a hand-built link line, and yields identical bytes
	@# everywhere except offset 7.
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

$(O)crt0.o: $(O)arch/$(ARCH)/crt0.o
	cp $< $@

$(O)crti.o: $(O)arch/$(ARCH)/crti.o
	cp $< $@

$(O)crtn.o: $(O)arch/$(ARCH)/crtn.o
	cp $< $@

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PICFLAGS) -c $< -o $@

$(O)%.pic.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libc.a $(O)libc.so $(O)libc.so.0 \
	      $(O)crt0.o $(O)crti.o $(O)crtn.o \
	      $(O)arch/$(ARCH)/crti.o $(O)arch/$(ARCH)/crtn.o

install: $(O)libc.a $(O)libc.so.0 $(O)crt0.o $(O)crti.o $(O)crtn.o
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)libc.a $(DESTDIR)/usr/$(LIBDIR)/
	install -m 755 $(O)libc.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libc.so.0 $(DESTDIR)/$(LIBDIR)/libc.so
	install -m 644 $(O)crt0.o $(DESTDIR)/usr/$(LIBDIR)/
	install -m 644 $(O)crti.o $(DESTDIR)/usr/$(LIBDIR)/
	install -m 644 $(O)crtn.o $(DESTDIR)/usr/$(LIBDIR)/
