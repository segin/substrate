include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../drivers/storage -I../../drivers/input -I../../arch/$(ARCH) -I../../arch/x86-common
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = usb.c usbdevfs.c uhci.c ehci.c xhci.c usb_msc.c usb_hid.c usb_hid_mouse.c usb_hub.c uac.c uas.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)usb_drv.o

$(O)usb_drv.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o *.d
