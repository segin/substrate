include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
RESOLV_CFLAGS     = $(CFLAGS) -I../../include
PIC_RESOLV_CFLAGS = $(RESOLV_CFLAGS)
else
RESOLV_CFLAGS     = $(USER_CFLAGS) -I.
PIC_RESOLV_CFLAGS = $(SHLIB_CFLAGS) -I.
endif

SRCS = res_init.c res_query.c res_mkquery.c res_send.c \
       ns_parse.c dn_expand.c getrrsetbyname.c h_errno.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: libresolv.a $(if $(NATIVE_BUILD),,libresolv.so.0)

libresolv.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

libresolv.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libresolv.so.0 \
	      -L../c -l:libc.so.0 -L../sys -l:libsys.so.0 \
	      -o $@ $(PIC_OBJS)
	@# Cross-ld stamps OSABI=SYSV (0); ld.so wants substrate (0x40).
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

%.o: %.c
	$(CC) $(RESOLV_CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PIC_RESOLV_CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.pic.o libresolv.a libresolv.so libresolv.so.0

install: libresolv.a $(if $(NATIVE_BUILD),,libresolv.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 libresolv.a $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 libresolv.so.0 $(DESTDIR)/lib/
	@ln -sf libresolv.so.0 $(DESTDIR)/lib/libresolv.so
endif
