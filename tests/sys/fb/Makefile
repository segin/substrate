# tests/sys/fb — bare-metal framebuffer + input smoke tests.
#
# fbmouse: opens /dev/fb0 + /dev/input/event0, takes the VT into
# KD_GRAPHICS, paints a recognizable background (dark blue) plus
# corner markers (yellow TL, magenta TR, cyan BL, green BR) and a
# red crosshair at exact screen center, then tracks a software
# cursor against the mouse.  Drives qemu input-send-event and the
# cursor centroid moves by the exact delta — the canonical test
# for "substrate fb+input plumbing is end-to-end working" without
# pulling in xorg-server.
#
# Build under the substrate cross-toolchain.  Drop the binary into
# the rootfs and boot with init=/tmp/fbmouse to see it run.
CROSS    ?= /opt/substrate/bin/i386-unknown-substrate-
CC       := $(CROSS)gcc
CFLAGS   ?= -O2 -Wall

all: fbmouse

fbmouse: fbmouse.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f fbmouse

.PHONY: all clean
