# sbin/sdm/Makefile — substrate display manager.
#
# sgreet is an Xlib client, so it links the contrib X client libraries
# (libX11/libxcb/libXau/libXdmcp).  The X11 stack under contrib/ must be
# built and staged (dist-libX11/, dist-libxcb/, ...) before this builds,
# so sdm is NOT in the default sbin SUBDIRS — build it after the X stack:
#
#   make -C sbin/sdm CROSS=/opt/substrate/bin/i386-unknown-substrate-
#
# It is dynamically linked; the .so.N libraries it needs already ship in
# the image under /usr/lib.

TOP      ?= ../..
CROSS    ?=
CC        = $(CROSS)gcc

X11_INC   = -I$(TOP)/dist-libX11/usr/include -I$(TOP)/dist-xorgproto/usr/include
X11_LIB   = -L$(TOP)/dist-libX11/usr/lib -L$(TOP)/dist-libxcb/usr/lib \
            -L$(TOP)/dist-libXau/usr/lib -L$(TOP)/dist-libXdmcp/usr/lib

CFLAGS   ?= -Wall -O2 -g -march=i486 -mtune=i486
CFLAGS   += $(X11_INC)
LDADD     = $(X11_LIB) -L$(TOP)/lib/sys -l:libsys.so.0 -lX11

all: sgreet

sgreet: sgreet.c
	$(CC) $(CFLAGS) $< -o $@ $(LDADD)

install: all
	install -d $(DESTDIR)/usr/sbin $(DESTDIR)/etc/X11 $(DESTDIR)/etc/sdm
	install -m 0755 sgreet   $(DESTDIR)/usr/sbin/sgreet
	install -m 0755 sdm.sh   $(DESTDIR)/usr/sbin/sdm
	install -m 0755 Xsession $(DESTDIR)/etc/X11/Xsession
	install -m 0644 sessions $(DESTDIR)/etc/sdm/sessions

clean:
	rm -f sgreet

.PHONY: all install clean
