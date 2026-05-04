include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
MATH_CFLAGS = $(CFLAGS) -I../../include
else
MATH_CFLAGS = $(USER_CFLAGS) -fno-builtin
endif

SRCS = src/math.c src/mathf.c src/mathl.c src/fpclassify.c src/fenv.c
OBJS = $(SRCS:.c=.o)

all: libm.a

libm.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(MATH_CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o libm.a

install: libm.a
	@mkdir -p $(DESTDIR)/usr/lib
	cp libm.a $(DESTDIR)/usr/lib/
