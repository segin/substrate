include ../../Makefile.inc
CFLAGS += -I../../include

SRCS = src/math.c src/fpclassify.c
OBJS = $(SRCS:.c=.o)

all: libm.a

libm.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o libm.a