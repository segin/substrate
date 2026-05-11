include ../../Makefile.inc

LIB   = libexvi.a
SHLIB = libexvi.so.0
SRCS  = exvi.c exvi_buffer.c exvi_parse.c exvi_runtime.c exvi_cmd.c exvi_session.c exvi_visual.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys/include -idirafter ../../sys
PIC_CFLAGS := $(CFLAGS)
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys/include -I../../sys
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include -I../../sys/include -I../../sys
endif

.PHONY: all clean install FORCE

all: $(LIB) $(if $(NATIVE_BUILD),,$(SHLIB))

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libexvi calls into libregex; declare DT_NEEDED so ld.so loads it.
$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/usr.lib/regex -l:libregex.so.0 \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(OBJS) $(PIC_OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(PIC_OBJS) $(LIB) $(SHLIB); \
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
	rm -f $(OBJS) $(PIC_OBJS) $(LIB) $(SHLIB) $(MODE_STAMP)

install: $(LIB) $(if $(NATIVE_BUILD),,$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp $(LIB) $(DESTDIR)/usr/lib/
	cp ../../include/exvi.h $(DESTDIR)/usr/include/
ifndef NATIVE_BUILD
	install -m 755 $(SHLIB) $(DESTDIR)/lib/
	@ln -sf $(SHLIB) $(DESTDIR)/lib/libexvi.so
endif
