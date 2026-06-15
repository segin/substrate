include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys -idirafter ../../sys/include -I./src
PIC_CFLAGS := $(CFLAGS)
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys -I../../sys/include -I./src
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include -I../../sys -I../../sys/include -I./src
endif

LIBNAME = libelfobj.a
SHLIB   = libelfobj.so.0
SRCS = src/elf_read.c src/elf_write.c src/elf_sections.c src/elf_symbols.c \
       src/elf_reloc.c src/elf_link.c src/elf_strtab.c src/elf_hash.c \
       src/elf_gnu_ext.c src/elf_dwarf.c src/elf_validate.c src/elf_layout.c \
       src/elf_util.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

.PHONY: all clean install test fuzz bench examples FORCE

all: $(LIBNAME) $(if $(NATIVE_BUILD),,$(SHLIB))

$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libelfobj is a leaf library (no DT_NEEDED beyond libc).
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

test: $(LIBNAME)
	$(MAKE) -C ../../tests/usr.lib/elfobj NATIVE_BUILD=1

fuzz: $(LIBNAME)
	$(MAKE) -C fuzz NATIVE_BUILD=1

bench: $(LIBNAME)
	$(MAKE) -C bench NATIVE_BUILD=1

examples: $(LIBNAME)
	$(MAKE) -C examples NATIVE_BUILD=1

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(LIBNAME) $(SHLIB) $(MODE_STAMP)
	$(MAKE) -C ../../tests/usr.lib/elfobj clean || true
	$(MAKE) -C fuzz clean || true
	$(MAKE) -C bench clean || true
	$(MAKE) -C examples clean || true

install: $(LIBNAME) $(if $(NATIVE_BUILD),,$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	@mkdir -p $(DESTDIR)/usr/include
	@mkdir -p $(DESTDIR)/usr/lib/pkgconfig
	cp $(LIBNAME) $(DESTDIR)/usr/lib/
	cp ../../include/elfobj.h $(DESTDIR)/usr/include/
	cp elfobj.pc $(DESTDIR)/usr/lib/pkgconfig/
	cp libelfobj.map $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 $(SHLIB) $(DESTDIR)/lib/
	@ln -sf $(SHLIB) $(DESTDIR)/lib/libelfobj.so
endif
