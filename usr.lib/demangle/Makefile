include ../../Makefile.inc

SRCS = buffer.c demangle.c itanium.c rust.c dlang.c
OBJS = $(SRCS:.c=.o)
LIBNAME = libdemangle.a
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

CFLAGS += -I../../include

.PHONY: all clean install FORCE

all: $(LIBNAME)

$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(LIBNAME); \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIBNAME) $(MODE_STAMP)

install: $(LIBNAME)
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	@mkdir -p $(DESTDIR)/usr/man/man3
	cp $(LIBNAME) $(DESTDIR)/usr/lib/
	cp ../../include/demangle.h $(DESTDIR)/usr/include/
	cp man3/*.3 $(DESTDIR)/usr/man/man3/
