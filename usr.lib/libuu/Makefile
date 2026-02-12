include ../../Makefile.inc

LIB = libuu.a
SRCS = uudecode.c
OBJS = $(SRCS:.c=.o)

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB)

install:
	@echo "Installing libuu..."
	# Usually we'd copy headers/libs to destdir, but for now just build it.
