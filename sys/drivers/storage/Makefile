include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../video -I../../arch/i386 -I../../arch/x86-common/include
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = blkdev.c scsi/scsi.c scsi/scsi_dev.c scsi/scsi_ctl.c scsi/atapi_scsi.c ide/ide.c ahci/ahci.c nvme/nvme.c ramdisk.c
OBJS = $(SRCS:.c=.o)

all: storage.o

storage.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o scsi/*.o ide/*.o ahci/*.o nvme/*.o
