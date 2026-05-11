include ../../Makefile.inc

# sbin/init is a shell script (etc/init.sh installed as /sbin/init);
# there's no compiled program to make dynamic.
PROG = init

.PHONY: all clean install

all:
	@true

clean:
	@true

install:
	@echo "Installing $(PROG) script..."
	install -m 755 ../../etc/init.sh $(DESTDIR)/sbin/$(PROG)
