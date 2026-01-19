include ../../../Makefile.inc

OBJS = virtio.o virtio_blk.o virtio_9p.o virtio_scsi.o

virtio_drv.o: $(OBJS)
	$(LD) -m elf_i386 -nostdlib -r -o virtio_drv.o $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -I../../include -I../../ -c -o $@ $<

clean:
	rm -f *.o
