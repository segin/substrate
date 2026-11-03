DYNAMIC = 1

include ../../Makefile.inc

PROG = nm
SRCS = nm.c
# A native (host) build of elfobj and demangle produces only the static
# archives, so link those; the target build links the shared libraries.
ifeq ($(NATIVE_BUILD),1)
LDADD += $(TOP)/usr.lib/elfobj/libelfobj.a $(TOP)/usr.lib/demangle/libdemangle.a
else
LDADD += -L$(TOP)/usr.lib/elfobj -l:libelfobj.so.0 -L$(TOP)/usr.lib/demangle -l:libdemangle.so.0
endif
USER_CFLAGS += -idirafter ../../include
EXTRA_CLEAN += .build-mode

BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

include ../../Makefile.bin.inc

.PHONY: FORCE

$(OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(PROG); \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

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
# $(O): see Makefile.inc (i386 in place, x86_64 under obj-x86_64/) -- both
# the program and the libraries it waits for live there.
$(O)$(PROG): ../../usr.lib/elfobj/$(O)libelfobj.so.0 ../../usr.lib/demangle/$(O)libdemangle.so.0

../../usr.lib/elfobj/$(O)libelfobj.so.0:
	$(MAKE) -C ../../usr.lib/elfobj

../../usr.lib/demangle/$(O)libdemangle.so.0:
	$(MAKE) -C ../../usr.lib/demangle
endif

# nm.1 lives in usr.man/man1 and is installed by usr.man/Makefile.
