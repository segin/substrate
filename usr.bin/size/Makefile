include ../../Makefile.inc

PROG = size
SRCS = size.c
LDADD += ../../usr.lib/elfobj/libelfobj.a
USER_CFLAGS += -idirafter ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

ifeq ($(NATIVE_BUILD),1)
.PHONY: elfobj-build-native

$(PROG): elfobj-build-native

elfobj-build-native:
	$(MAKE) -C ../../usr.lib/elfobj clean
	$(MAKE) -C ../../usr.lib/elfobj NATIVE_BUILD=1
else
$(PROG): ../../usr.lib/elfobj/libelfobj.a

../../usr.lib/elfobj/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elfobj
endif
