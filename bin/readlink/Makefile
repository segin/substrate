PROG = readlink
SRCS = readlink.c

# canon() concatenates path components into PATH_MAX-sized scratch buffers
# guarded by explicit ENAMETOOLONG checks, so gcc's format-truncation
# analysis (which can't see the runtime guards) is a false positive here.
USER_CFLAGS += -Wno-format-truncation

DYNAMIC = 1

include ../../Makefile.inc

OBJS = $(SRCS:.c=.o)

include ../../Makefile.bin.inc
