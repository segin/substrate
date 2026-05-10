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
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

.PHONY: all clean install FORCE

all: libusb.a $(if $(NATIVE_BUILD),,libusb.so.0)

libusb.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libusb depends on libc.so.0.
libusb.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libusb.so.0 \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(OBJS) $(PIC_OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(PIC_OBJS) libusb.a libusb.so libusb.so.0; \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PIC_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) libusb.a libusb.so libusb.so.0 $(MODE_STAMP)

install: libusb.a $(if $(NATIVE_BUILD),,libusb.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp libusb.a $(DESTDIR)/usr/lib/
	cp include/libusb.h $(DESTDIR)/usr/include/
ifndef NATIVE_BUILD
	install -m 755 libusb.so.0 $(DESTDIR)/lib/
	@ln -sf libusb.so.0 $(DESTDIR)/lib/libusb.so
endif
