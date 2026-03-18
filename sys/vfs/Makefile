include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = vfs.c vnode.c vfs_lookup.c vfs_cache.c vfs_mount.c vnode_ops.c bio.c
OBJS = $(SRCS:.c=.o)

all: vfs_subsys.o

vfs_subsys.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
