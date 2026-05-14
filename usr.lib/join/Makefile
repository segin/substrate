include ../../Makefile.inc

SRCS     = libjoin.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)
LIBNAME  = libjoin.a
SHLIB    = libjoin.so.0
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys -idirafter ../../sys/include
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys -I../../sys/include
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include -I../../sys -I../../sys/include
endif

.PHONY: all clean install FORCE

all: $(LIBNAME) $(if $(NATIVE_BUILD),,$(SHLIB))

$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# Versioned soname + DT_NEEDED libc so the resulting .so is a proper
# substrate-target shared object (matches lib/c, lib/m, demangle,
# elfobj, etc. conventions).
$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(OBJS) $(PIC_OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(PIC_OBJS) $(LIBNAME) $(SHLIB); \
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
	rm -f $(OBJS) $(PIC_OBJS) $(LIBNAME) $(SHLIB) $(MODE_STAMP)

install: $(LIBNAME) $(if $(NATIVE_BUILD),,$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp $(LIBNAME) $(DESTDIR)/usr/lib/
	cp ../../include/libjoin.h $(DESTDIR)/usr/include/
ifndef NATIVE_BUILD
	install -m 755 $(SHLIB) $(DESTDIR)/lib/
	@ln -sf $(SHLIB) $(DESTDIR)/lib/libjoin.so
endif
