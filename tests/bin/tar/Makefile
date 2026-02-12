# Tests for tar
TOP = ../../..
include $(TOP)/Makefile.inc

PROG = test_tar
SRCS = test_tar.c \
       $(TOP)/bin/tar/buffer.c \
       $(TOP)/bin/tar/header.c \
       $(TOP)/bin/tar/create.c \
       $(TOP)/bin/tar/extract.c \
       $(TOP)/bin/tar/list.c \
       $(TOP)/bin/tar/util.c

OBJS = $(SRCS:.c=.o)

LDFLAGS := $(USER_LDFLAGS)
LIBC_DIR = $(TOP)/lib/c

.PHONY: all clean

all: $(PROG)

ifdef NATIVE_BUILD
    # Native build
    CRT0 =
    LIBS =
    # Do not include Substrate headers for native build
    INC_FLAGS = -I$(TOP)/bin/tar
else
    # Substrate build
    CRT0 = $(LIBC_DIR)/crt0.o
    LIBS = -L$(LIBC_DIR) -lc $(LIBGCC)
    INC_FLAGS = -I$(TOP)/bin/tar -I$(TOP)/include
endif

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(CRT0) $(LIBS)

%.o: %.c
	$(CC) $(USER_CFLAGS) $(INC_FLAGS) -DTEST -c $< -o $@

clean:
	rm -f $(PROG) $(OBJS)
