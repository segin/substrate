include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = vga.c fb.c fb_console.c fb_ops.c font_8x16.c font_8x8.c font_cache.c psf.c bdf_pcf.c bga.c hw_text.c bios_menu.c
OBJS = $(SRCS:.c=.o)

all: video.o

video.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
