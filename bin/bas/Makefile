PROG = bas
SRCS = bas.c compile.c run.c

DYNAMIC = 1

include ../../Makefile.inc

ifdef NATIVE_BUILD
    LDADD = -lm
else
    # Name the shared object outright, as every other target here does.
    # A plain -lm against the build tree picks up libm.a: ld tries only
    # `libm.so` and `libm.a` per -L directory, and lib/m deliberately ships
    # no `libm.so` symlink (see lib/c/Makefile -- it would make -lc prefer
    # the shared libc and break static links).  The sysroot has the linker
    # name, but -L$(TOP)/lib/m is searched first, so -l: is what actually
    # gets bas onto the shared libm.
    LDADD = -L$(TOP)/lib/m -l:libm.so.0
endif

include ../../Makefile.bin.inc
