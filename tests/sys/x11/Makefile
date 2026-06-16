# tests/sys/x11 — substrate X11 client tracers.
#
# xtrace: minimal Xlib client that logs every round-trip it makes to
# the X server.  Drop into the rootfs and run from a telnet/getty
# shell after starting Xfbdev to see exactly which Xlib call the
# server SEGVs on:
#
#     DISPLAY=:0 /xtrace 2>/var/log/xtrace.log
#
# The last line in /var/log/xtrace.log immediately before the
# "X IO ERROR: display closed" line is the request that killed the
# server.
CROSS    ?= /opt/substrate/bin/i386-unknown-substrate-
CC       := $(CROSS)gcc

LIBX11_STAGE ?= $(CURDIR)/../../../dist-overlay/dist-libX11
LIBXAU_STAGE ?= $(CURDIR)/../../../dist-overlay/dist-libXau
LIBXCB_STAGE ?= $(CURDIR)/../../../dist-overlay/dist-libxcb
LIBXDMCP_STAGE ?= $(CURDIR)/../../../dist-overlay/dist-libXdmcp
XORGPROTO_STAGE ?= $(CURDIR)/../../../dist-overlay/dist-xorgproto

CFLAGS   ?= -O2 -Wall -g \
            -I$(LIBX11_STAGE)/usr/include \
            -I$(XORGPROTO_STAGE)/usr/include

LDFLAGS  ?= -L$(LIBX11_STAGE)/usr/lib \
            -L$(LIBXAU_STAGE)/usr/lib \
            -L$(LIBXCB_STAGE)/usr/lib \
            -L$(LIBXDMCP_STAGE)/usr/lib \
            -Wl,-rpath,/usr/lib

LDLIBS   ?= -lX11 -lxcb -lXau -lXdmcp -lpthread

all: xtrace

xtrace: xtrace.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -f xtrace

.PHONY: all clean
