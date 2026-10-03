include ../../Makefile.inc

ifdef NATIVE_BUILD
CFLAGS := $(CFLAGS) -DNATIVE_BUILD=1 -Iinclude -Ios -idirafter ../../sys/include -I../../sys
PIC_CFLAGS := $(CFLAGS)
else
CFLAGS := $(CFLAGS) -Iinclude -Ios -I../../include -I../../sys/include -I../../sys
PIC_CFLAGS := $(SHLIB_CFLAGS) -Iinclude -Ios -I../../include -I../../sys/include -I../../sys
endif

SRCS = src/core.c src/device.c src/sync.c src/descriptor.c src/async.c src/poll.c \
	os/substrate.c
# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = $(O).build-mode

.PHONY: all clean install FORCE

all: $(O)libusb.a $(if $(NATIVE_BUILD),,$(O)libusb.so.0)

$(O)libusb.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libusb depends on libc.so.0.
$(O)libusb.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libusb.so.0 \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(OBJS) $(PIC_OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mkdir -p $(dir $@)
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(PIC_OBJS) $(O)libusb.a $(O)libusb.so $(O)libusb.so.0; \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PIC_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libusb.a $(O)libusb.so $(O)libusb.so.0 $(MODE_STAMP)

install: $(O)libusb.a $(if $(NATIVE_BUILD),,$(O)libusb.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	@mkdir -p $(DESTDIR)/usr/include
	cp $(O)libusb.a $(DESTDIR)/usr/$(LIBDIR)/
	cp include/libusb.h $(DESTDIR)/usr/include/
ifndef NATIVE_BUILD
	install -m 755 $(O)libusb.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libusb.so.0 $(DESTDIR)/$(LIBDIR)/libusb.so
endif
