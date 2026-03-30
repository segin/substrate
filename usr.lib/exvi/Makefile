include ../../Makefile.inc

LIB = libexvi.a
SRCS = exvi.c exvi_buffer.c exvi_parse.c exvi_runtime.c exvi_cmd.c exvi_session.c exvi_visual.c
OBJS = $(SRCS:.c=.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys/include -idirafter ../../sys
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys/include -I../../sys
endif

.PHONY: all clean install FORCE

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(LIB); \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB) $(MODE_STAMP)

install: $(LIB)
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp $(LIB) $(DESTDIR)/usr/lib/
	cp ../../include/exvi.h $(DESTDIR)/usr/include/
