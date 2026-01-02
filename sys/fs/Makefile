include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../drivers/video -I../vfs
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = ext2/ext2.c fat/fat.c exfat/exfat.c minix/minix.c devfs.c procfs.c sysfs.c fuse.c

OBJS = $(SRCS:.c=.o)



all: fs.o



fs.o: $(OBJS)

	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)



clean:

	rm -f *.o ext2/*.o fat/*.o exfat/*.o minix/*.o
