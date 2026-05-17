include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = ext2/ext2.c ext2/ext2_hash.c fat/fat.c exfat/exfat.c minix/minix.c sysv/sysv.c udf/udf.c udf/udf_write.c devfs.c procfs.c sysfs.c shmfs.c fuse.c 9p.c pipe.c

OBJS = $(SRCS:.c=.o)



all: fs.o



fs.o: $(OBJS)

	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)



clean:

	rm -f *.o ext2/*.o fat/*.o exfat/*.o minix/*.o sysv/*.o udf/*.o
