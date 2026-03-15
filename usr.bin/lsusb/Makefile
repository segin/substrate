include ../../Makefile.inc

PROG = lsusb
SRCS = lsusb.c

LIBUSB_DIR = ../../lib/usb

ifdef NATIVE_BUILD
  ifdef USE_SYSTEM_LIBUSB
    # Host build using the system libusb-1.0 (real USB access)
    USER_CFLAGS += $(shell pkg-config --cflags libusb-1.0)
    LDADD += $(shell pkg-config --libs libusb-1.0)
  else
    # Host build using Substrate's libusb (stub/test)
    USER_CFLAGS += -I$(LIBUSB_DIR)/include -idirafter ../../sys/include -I../../sys
    LDADD += $(LIBUSB_DIR)/libusb.a
  endif
else
  # Target (Substrate) build
  USER_CFLAGS += -I$(LIBUSB_DIR)/include
  LDADD += $(LIBUSB_DIR)/libusb.a
endif

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

# Library dependency — rebuild if libusb changes
ifndef USE_SYSTEM_LIBUSB
$(PROG): $(LIBUSB_DIR)/libusb.a

$(LIBUSB_DIR)/libusb.a:
	$(MAKE) -C $(LIBUSB_DIR) $(if $(NATIVE_BUILD),NATIVE_BUILD=1)
endif

.PHONY: install-man
install: install-man
install-man:
	install -D ../../usr.man/man1/lsusb.1 $(DESTDIR)/usr/man/man1/lsusb.1
