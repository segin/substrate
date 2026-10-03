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
# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

all: $(O)libresolv.a $(if $(NATIVE_BUILD),,$(O)libresolv.so.0)

$(O)libresolv.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(O)libresolv.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libresolv.so.0 \
	      -L../c/$(O) -l:libc.so.0 -L../sys/$(O) -l:libsys.so.0 \
	      -o $@ $(PIC_OBJS)
	@# Cross-ld stamps OSABI=SYSV (0); ld.so wants substrate (0x40).
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(RESOLV_CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PIC_RESOLV_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libresolv.a $(O)libresolv.so $(O)libresolv.so.0

install: $(O)libresolv.a $(if $(NATIVE_BUILD),,$(O)libresolv.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)libresolv.a $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)libresolv.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libresolv.so.0 $(DESTDIR)/$(LIBDIR)/libresolv.so
endif
