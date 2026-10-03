include ../../../Makefile.inc

OBJS = $(addprefix $(O),virtio.o virtio_blk.o virtio_9p.o virtio_scsi.o virtio_gpu.o virtio_net.o)

all: $(O)virtio_drv.o

$(O)virtio_drv.o: $(OBJS)
	$(LD) $(FREESTANDING_LDFLAGS) -r -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -I../../include -I../../ -c -o $@ $<

clean:
	rm -f *.o *.d
