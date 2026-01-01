include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = vfs.c
OBJS = $(SRCS:.c=.o)

all: vfs_subsys.o

vfs_subsys.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
