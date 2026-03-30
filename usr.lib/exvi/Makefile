include ../../Makefile.inc

LIB = libexvi.a
SRCS = exvi.c exvi_buffer.c exvi_parse.c
OBJS = $(SRCS:.c=.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys/include -idirafter ../../sys
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys/include -I../../sys
endif

.PHONY: all clean install
.PHONY: ensure_build_mode

all: $(LIB)

ensure_build_mode:
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(LIB); \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

$(LIB): ensure_build_mode $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c | ensure_build_mode
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB) $(MODE_STAMP)

install: $(LIB)
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp $(LIB) $(DESTDIR)/usr/lib/
	cp ../../include/exvi.h $(DESTDIR)/usr/include/
