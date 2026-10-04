DYNAMIC = 1

include ../../Makefile.inc

PROG = size
SRCS = size.c
LDADD += -L$(TOP)/usr.lib/elfobj -l:libelfobj.so.0
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
# $(O): see Makefile.inc (i386 in place, x86_64 under obj-x86_64/) -- both
# the program and the library it waits for live there.
$(O)$(PROG): ../../usr.lib/elfobj/$(O)libelfobj.so.0

../../usr.lib/elfobj/$(O)libelfobj.so.0:
	$(MAKE) -C ../../usr.lib/elfobj
endif

.PHONY: install-man

install: install-man

install-man:
	mkdir -p $(DESTDIR)/usr/man/man1
	install -m 0644 size.1 $(DESTDIR)/usr/man/man1/size.1
