include ../../Makefile.inc

ifdef NATIVE_BUILD
CFLAGS := $(CFLAGS) -DNATIVE_BUILD=1 -Iinclude -idirafter ../../sys/include -I../../sys
else
CFLAGS := $(CFLAGS) -Iinclude -I../../include -I../../sys/include -I../../sys
endif

SRCS = src/core.c src/device.c src/sync.c src/descriptor.c src/async.c src/poll.c
OBJS = $(SRCS:.c=.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

.PHONY: all clean install FORCE

all: libusb.a

libusb.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) libusb.a; \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libusb.a $(MODE_STAMP)

install: libusb.a
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp libusb.a $(DESTDIR)/usr/lib/
	cp include/libusb.h $(DESTDIR)/usr/include/
