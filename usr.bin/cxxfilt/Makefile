include ../../Makefile.inc

PROG = c++filt
SRCS = cxxfilt.c
OBJS = $(SRCS:.c=.o)

# Dynamic-link the target build (mirrors Makefile.bin.inc DYNAMIC=1).
TARGET_CFLAGS = $(filter-out -fno-pie,$(USER_CFLAGS)) -fPIE
TARGET_LDFLAGS = -m32 -nostdlib -pie -fPIE \
                 -Wl,--dynamic-linker=/sbin/ld.so \
                 -Wl,--unresolved-symbols=ignore-in-shared-libs

DEMANGLE_SHLIB = ../../usr.lib/demangle/libdemangle.so.0

.PHONY: all clean install

all: $(PROG)

$(PROG): $(OBJS) $(DEMANGLE_SHLIB)
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) -o $@ $(OBJS) ../../usr.lib/demangle/libdemangle.a
else
	$(CC) $(TARGET_LDFLAGS) -o $@ $(OBJS) $(TOP)/lib/c/crt0.o \
	      -L$(TOP)/usr.lib/demangle -l:libdemangle.so.0 \
	      -L$(TOP)/lib/c -l:libc.so.0 $(LIBGCC)
	$(call BRAND_SUBSTRATE,$@)
endif

$(DEMANGLE_SHLIB):
	$(MAKE) -C ../../usr.lib/demangle $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

%.o: %.c
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) -idirafter ../../include -c $< -o $@
else
	$(CC) $(TARGET_CFLAGS) -c $< -o $@
endif

clean:
	rm -f $(PROG) $(OBJS)

install: $(PROG)
	@mkdir -p $(DESTDIR)/usr/bin
	@mkdir -p $(DESTDIR)/usr/man/man1
	cp $(PROG) $(DESTDIR)/usr/bin/$(PROG)
	cp c++filt.1 $(DESTDIR)/usr/man/man1/
