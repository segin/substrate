PROG = bas
SRCS = bas.c compile.c run.c

include ../../Makefile.inc

ifdef NATIVE_BUILD
    LDADD = -lm
else
    LDADD = -L../../lib/m -lm
endif

include ../../Makefile.bin.inc
