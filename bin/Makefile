include ../Makefile.inc

# Subdirectories for core binaries.  One name per line so adding a
# new tool is a one-line diff and `git blame` is useful.
SUBDIRS = \
	arch \
	audioctl \
	awk \
	bas \
	basename \
	bc \
	cal \
	cat \
	chgrp \
	chmod \
	chown \
	chroot \
	clear \
	cmp \
	comm \
	cp \
	cut \
	date \
	dc \
	devtree \
	df \
	diff \
	dirname \
	dmesg \
	echo \
	ed \
	ex \
	false \
	find \
	fmt \
	fold \
	getty \
	grep \
	groups \
	head \
	hostname \
	id \
	join \
	kill \
	last \
	ldtctl \
	ln \
	login \
	ls \
	lspci \
	mkdir \
	mknod \
	more \
	mount \
	mv \
	native_test \
	newgrp \
	nice \
	nproc \
	od \
	passwd \
	paste \
	pgrep \
	ping \
	printf \
	prof \
	ps \
	pwd \
	readlink \
	renice \
	reset \
	rm \
	rmdir \
	sed \
	size \
	sleep \
	sort \
	split \
	stty \
	su \
	sum \
	sync \
	tabs \
	tail \
	tc \
	tee \
	test \
	time \
	top \
	touch \
	tr \
	true \
	tty \
	umount \
	uname \
	uniq \
	uptime \
	vi \
	w \
	wall \
	wc \
	who \
	whoami \
	write \
	xargs \
	xenix \
	yes

.PHONY: all clean install $(SUBDIRS)

all: usr_libs $(SUBDIRS)

usr_libs:
	@echo ">>> Entering usr.lib"
	$(MAKE) -C ../usr.lib NATIVE_BUILD="$(NATIVE_BUILD)"
	@echo "<<< Leaving usr.lib"

$(SUBDIRS): usr_libs

$(SUBDIRS):
	@echo ">>> Entering bin/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving bin/$@"

clean:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Cleaning bin/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Installing bin/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
