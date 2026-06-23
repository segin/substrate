include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../drivers/storage -I../../drivers/input -I../../arch/i386 -I../../arch/x86-common
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = usb.c uhci.c usb_msc.c usb_hid.c usb_hid_mouse.c usb_hub.c uac.c
OBJS = $(SRCS:.c=.o)

all: usb_drv.o

usb_drv.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
