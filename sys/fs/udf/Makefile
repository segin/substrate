# Makefile for UDF filesystem driver

OBJS = udf.o

udf.o: udf.c udf.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS)
