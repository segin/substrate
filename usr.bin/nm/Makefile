include ../../Makefile.inc

PROG = nm
SRCS = nm.c
LDADD += ../../usr.lib/elfobj/libelfobj.a ../../usr.lib/demangle/libdemangle.a
USER_CFLAGS += -idirafter ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

ifeq ($(NATIVE_BUILD),1)
.PHONY: elfobj-build-native demangle-build-native

$(PROG): elfobj-build-native demangle-build-native

elfobj-build-native:
	$(MAKE) -C ../../usr.lib/elfobj clean
	$(MAKE) -C ../../usr.lib/elfobj NATIVE_BUILD=1

demangle-build-native:
	$(MAKE) -C ../../usr.lib/demangle clean
	$(MAKE) -C ../../usr.lib/demangle NATIVE_BUILD=1
else
$(PROG): ../../usr.lib/elfobj/libelfobj.a ../../usr.lib/demangle/libdemangle.a

../../usr.lib/elfobj/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elfobj

../../usr.lib/demangle/libdemangle.a:
	$(MAKE) -C ../../usr.lib/demangle
endif

.PHONY: install-man

install: install-man

install-man:
	mkdir -p $(DESTDIR)/usr/man/man1
	install -m 0644 nm.1 $(DESTDIR)/usr/man/man1/nm.1
