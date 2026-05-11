DYNAMIC = 1

include ../../Makefile.inc

PROG = nm
SRCS = nm.c
LDADD += -L$(TOP)/usr.lib/elfobj -l:libelfobj.so.0 -L$(TOP)/usr.lib/demangle -l:libdemangle.so.0
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
$(PROG): ../../usr.lib/elfobj/libelfobj.so.0 ../../usr.lib/demangle/libdemangle.so.0

../../usr.lib/elfobj/libelfobj.so.0:
	$(MAKE) -C ../../usr.lib/elfobj

../../usr.lib/demangle/libdemangle.so.0:
	$(MAKE) -C ../../usr.lib/demangle
endif

.PHONY: install-man

install: install-man

install-man:
	mkdir -p $(DESTDIR)/usr/man/man1
	install -m 0644 nm.1 $(DESTDIR)/usr/man/man1/nm.1
