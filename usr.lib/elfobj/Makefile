include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys -idirafter ../../sys/include -I./src
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys -I../../sys/include -I./src
endif

LIBNAME = libelfobj.a
SRCS = src/elf_read.c src/elf_write.c src/elf_sections.c src/elf_symbols.c \
       src/elf_reloc.c src/elf_link.c src/elf_strtab.c src/elf_hash.c \
       src/elf_gnu_ext.c src/elf_dwarf.c src/elf_validate.c src/elf_layout.c \
       src/elf_util.c
OBJS = $(SRCS:.c=.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

.PHONY: all clean install test fuzz bench examples
.PHONY: ensure_build_mode

all: $(LIBNAME)

ensure_build_mode:
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

$(LIBNAME): ensure_build_mode $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(OBJS): $(MODE_STAMP)

$(MODE_STAMP):
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(LIBNAME)
	$(MAKE) -C tests NATIVE_BUILD=1

fuzz: $(LIBNAME)
	$(MAKE) -C fuzz NATIVE_BUILD=1

bench: $(LIBNAME)
	$(MAKE) -C bench NATIVE_BUILD=1

examples: $(LIBNAME)
	$(MAKE) -C examples NATIVE_BUILD=1

clean:
	rm -f $(OBJS) $(LIBNAME) $(MODE_STAMP)
	$(MAKE) -C tests clean || true
	$(MAKE) -C fuzz clean || true
	$(MAKE) -C bench clean || true
	$(MAKE) -C examples clean || true

install: $(LIBNAME)
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	@mkdir -p $(DESTDIR)/usr/lib/pkgconfig
	cp $(LIBNAME) $(DESTDIR)/usr/lib/
	cp ../../include/elfobj.h $(DESTDIR)/usr/include/
	cp elfobj.pc $(DESTDIR)/usr/lib/pkgconfig/
	cp libelfobj.map $(DESTDIR)/usr/lib/
