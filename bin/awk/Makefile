TOP = ../..
include $(TOP)/Makefile.inc

PROG = awk

# Upstream One True Awk lives in contrib/onetrueawk/.  We compile
# its sources verbatim — no local patches, no shim files in
# bin/awk/ itself.  VPATH lets the pattern rule in Makefile.bin.inc
# find each .c by basename while object files still land here.
AWK_SRCDIR = $(TOP)/contrib/onetrueawk
VPATH      = $(AWK_SRCDIR)

SRCS = awkgram.tab.c b.c main.c parse.c proctab.c tran.c lib.c run.c lex.c

# -DHAS_ISBLANK silences a wctype.h fallback in awk.h.
# -Wno-* knock out warnings upstream is aware of but doesn't fix
# because the affected paths are correct in practice (Lucent
# never adopted -Werror).  Substrate's bin/ Makefiles do use
# -Werror, so we have to relax it for the vendored tree.
USER_CFLAGS += -I$(AWK_SRCDIR) -I$(TOP)/include -I$(TOP)/sys/include \
               -DHAS_ISBLANK \
               -Wno-error -Wno-parentheses -Wno-unused-parameter \
               -Wno-sign-compare -Wno-unused-but-set-variable \
               -Wno-unused-function -Wno-implicit-fallthrough \
               -Wno-misleading-indentation -Wno-pointer-sign \
               -Wno-format -Wno-deprecated-declarations

# awk uses math.h heavily (modf, log, pow, sin, cos, sqrt, atan2,
# exp).  libc.so.0 declares DT_NEEDED libm.so.0 so the resolution
# happens at runtime, but ld still needs libm on the static link
# line to satisfy direct references from awk's objects.
LDADD = -L$(TOP)/lib/m -l:libm.so.0

DYNAMIC = 1

include $(TOP)/Makefile.bin.inc
