# tests/sbin/ld64 - target tests for /sbin/ld64.so, the 64-bit dynamic
# linker.  Every program here is a 64-bit PIE whose PT_INTERP is
# /sbin/ld64.so, linked against the 64-bit shared libraries of
# `make -C lib ARCH=x86_64`; they run on the x86_64 kernel only.
#
#     make -C lib ARCH=x86_64
#     make -C sbin/ld.so ARCH=x86_64
#     make -C tests/sbin/ld64
#
# On the target: ld64.so in /sbin, the libraries and libld64mod.so in
# /lib64, the programs anywhere; run-ld64-tests.sh runs them all and
# prints one OK/FAIL line each.
#
# hello     printf, argv, envp, getenv
# tls       __thread in the executable, errno
# math      libm.so.0
# threads   libpthread.so.0: threads, mutex, keys, per-thread __thread
# dlmain    dlopen/dlsym/dlclose of libld64mod.so (dlmod.c)

ARCH := x86_64
include ../../../Makefile.inc

B = obj-x86_64/
PROGS = hello tls math threads dlmain
LIBD  = $(TOP)/lib

T_CFLAGS  = $(filter-out -fno-pie,$(USER_CFLAGS)) -fPIE -O1
T_LDFLAGS = -m64 -nostdlib -pie \
            -Wl,--dynamic-linker=/sbin/ld64.so \
            -Wl,--unresolved-symbols=ignore-in-shared-libs \
            -Wl,-z,max-page-size=0x1000
CRT0      = $(LIBD)/c/$(B)crt0.o
T_LIBS    = -L$(LIBD)/c/$(B) -l:libc.so.0 -L$(LIBD)/sys/$(B) -l:libsys.so.0 \
            $(LIBGCC)

# Libraries a test needs beyond libc.  -l:libdl.so.0, not libdl.a: the
# archive's weak __ldso_dlopen reference is resolved to 0 at link time.
LIBS_math    = -L$(LIBD)/m/$(B) -l:libm.so.0
LIBS_threads = -L$(LIBD)/pthread/$(B) -l:libpthread.so.0
LIBS_dlmain  = -L$(LIBD)/dl/$(B) -l:libdl.so.0

.PHONY: all clean

all: $(PROGS:%=$(B)%) $(B)libld64mod.so

$(B)%.o: %.c
	@mkdir -p $(B)
	$(CC) $(T_CFLAGS) -c $< -o $@

$(B)%: $(B)%.o
	$(CC) $(T_LDFLAGS) -o $@ $< $(CRT0) $(LIBS_$*) $(T_LIBS)
	$(call BRAND_SUBSTRATE,$@)

$(B)dlmod.pic.o: dlmod.c
	@mkdir -p $(B)
	$(CC) $(SHLIB_CFLAGS) -c $< -o $@

$(B)libld64mod.so: $(B)dlmod.pic.o
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libld64mod.so -o $@ $< \
	      -L$(LIBD)/c/$(B) -l:libc.so.0
	$(call BRAND_SUBSTRATE,$@)

clean:
	rm -rf $(B)
