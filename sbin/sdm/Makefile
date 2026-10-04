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

# ARCH=x86_64 builds the 64-bit sgreet as obj-x86_64/sgreet, against the
# 64-bit X stack contrib/port64.sh staged under dist-overlay64/:
#
#   make -C sbin/sdm ARCH=x86_64 CROSS=/opt/substrate/bin/x86_64-unknown-substrate-
ifeq ($(ARCH),x86_64)
O         = obj-x86_64/
OVERLAY   = $(TOP)/dist-overlay64
XLIBDIR   = usr/lib64
SYSLIB    = $(TOP)/lib/sys/obj-x86_64
ARCHFLAGS =
else
O         =
OVERLAY   = $(TOP)/dist-overlay
XLIBDIR   = usr/lib
SYSLIB    = $(TOP)/lib/sys
ARCHFLAGS = -march=i486 -mtune=i486
endif

X11_INC   = -I$(OVERLAY)/dist-libX11/usr/include -I$(OVERLAY)/dist-xorgproto/usr/include
X11_LIB   = -L$(OVERLAY)/dist-libX11/$(XLIBDIR) -L$(OVERLAY)/dist-libxcb/$(XLIBDIR) \
            -L$(OVERLAY)/dist-libXau/$(XLIBDIR) -L$(OVERLAY)/dist-libXdmcp/$(XLIBDIR)

CFLAGS   ?= -Wall -O2 -g $(ARCHFLAGS)
CFLAGS   += $(X11_INC)
LDADD     = $(X11_LIB) -L$(SYSLIB) -l:libsys.so.0 -lX11

all: $(O)sgreet

$(O)sgreet: sgreet.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@ $(LDADD)

install: all
	install -d $(DESTDIR)/usr/sbin $(DESTDIR)/etc/X11 $(DESTDIR)/etc/sdm
	install -m 0755 $(O)sgreet $(DESTDIR)/usr/sbin/sgreet
	install -m 0755 sdm.sh   $(DESTDIR)/usr/sbin/sdm
	install -m 0755 Xsession $(DESTDIR)/etc/X11/Xsession
	install -m 0755 starttde-substrate $(DESTDIR)/etc/X11/starttde-substrate
	install -m 0644 sessions $(DESTDIR)/etc/sdm/sessions

clean:
	rm -f sgreet obj-x86_64/sgreet

.PHONY: all install clean
