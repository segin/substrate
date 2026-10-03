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
# ifunc     indirect functions: IRELATIVE in the program, an STT_GNU_IFUNC
#           symbol of libld64if.so (ifmod.c)
# lazy      lazy binding: libld64lazy.so (lazymod.c) through the binding
#           trampoline, an undefined function that is only fatal when
#           called, LD_BIND_NOW, dlopen(RTLD_NOW) of libld64undef.so
#           (lazyundef.c)
# rtldnext  dlsym(RTLD_NEXT) from libld64next.so (nextmod.c)

ARCH := x86_64
include ../../../Makefile.inc

B = obj-x86_64/
PROGS = hello tls math threads dlmain ifunc lazy rtldnext
MODS  = libld64mod.so libld64if.so libld64lazy.so libld64undef.so \
        libld64next.so
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
LIBDL        = -L$(LIBD)/dl/$(B) -l:libdl.so.0
LIBS_dlmain  = $(LIBDL)
LIBS_ifunc   = -L$(B) -l:libld64if.so $(LIBDL)
# lazy calls ld64_absent(), which must have a PLT slot but no definition
# at run time: it is linked against libld64lazy-link.so, which has the
# soname of libld64lazy.so and additionally defines that one function
# (lazystub.c), and is not installed.
LIBS_lazy    = -L$(B) -l:libld64lazy-link.so $(LIBDL)
# libld64next.so ahead of libc.so.0, so that its atoi() is found first.
LIBS_rtldnext = -L$(B) -l:libld64next.so $(LIBDL)

# The test modules are built the way a third-party library would be,
# not the way the system libraries are: no -z now (so their PLT slots
# are bound lazily) and no -Bsymbolic-functions (so a call to their own
# exported function goes through the PLT).
MOD_LDFLAGS = $(filter-out -Wl$(comma)-z$(comma)now \
                -Wl$(comma)-Bsymbolic-functions,$(SHLIB_LDFLAGS)) -Wl,-z,lazy
comma := ,

# The source of each module, and what it links beyond libc.
SRC_libld64mod.so   = dlmod
SRC_libld64if.so    = ifmod
SRC_libld64lazy.so  = lazymod
SRC_libld64undef.so = lazyundef
SRC_libld64next.so  = nextmod
LIBS_libld64next.so = $(LIBDL)

.PHONY: all clean

all: $(MODS:%=$(B)%) $(PROGS:%=$(B)%)

$(B)%.o: %.c
	@mkdir -p $(B)
	$(CC) $(T_CFLAGS) -c $< -o $@

$(B)ifunc:    $(B)libld64if.so
$(B)lazy:     $(B)libld64lazy-link.so
$(B)rtldnext: $(B)libld64next.so

$(B)%: $(B)%.o
	$(CC) $(T_LDFLAGS) -o $@ $< $(CRT0) $(LIBS_$*) $(T_LIBS)
	$(call BRAND_SUBSTRATE,$@)

$(B)libld64lazy-link.so: $(B)lazymod.pic.o $(B)lazystub.pic.o
	$(CC) $(MOD_LDFLAGS) -Wl,-soname,libld64lazy.so -o $@ $^ \
	      -L$(LIBD)/c/$(B) -l:libc.so.0

$(B)%.pic.o: %.c
	@mkdir -p $(B)
	$(CC) $(SHLIB_CFLAGS) -c $< -o $@

.SECONDEXPANSION:
$(MODS:%=$(B)%): $(B)%: $(B)$$(SRC_%).pic.o
	$(CC) $(MOD_LDFLAGS) -Wl,-soname,$* -o $@ $< $(LIBS_$*) \
	      -L$(LIBD)/c/$(B) -l:libc.so.0
	$(call BRAND_SUBSTRATE,$@)

clean:
	rm -rf $(B)
