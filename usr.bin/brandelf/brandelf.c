/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2000, 2001 David O'Brien
 * Copyright (c) 1996 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <elf.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>

/* Fallback for missing headers on host/target */
#ifndef ELFOSABI_SYSV
#define ELFOSABI_SYSV 0
#endif
#ifndef ELFOSABI_LINUX
#define ELFOSABI_LINUX 3
#endif
#ifndef ELFOSABI_FREEBSD
#define ELFOSABI_FREEBSD 9
#endif
#ifndef ELFOSABI_SOLARIS
#define ELFOSABI_SOLARIS 6
#endif

/* Custom TestUnix/SVR4 values */
#define ELFOSABI_TESTUNIX 64
#define ELFOSABI_ATT_UNIX 65 

#ifndef nitems
#define nitems(x) (sizeof((x)) / sizeof((x)[0]))
#endif

/* Minimal err.h replacement */
static void err(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "brandelf: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ": %s\n", strerror(errno));
    va_end(ap);
    exit(eval);
}

static void errx(int eval, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "brandelf: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(eval);
}

static void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "brandelf: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ": %s\n", strerror(errno));
    va_end(ap);
}

static void warnx(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "brandelf: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static int elftype(const char *);
static const char *iselftype(int);
static void printelftypes(void);
static void usage(void); // __dead2;

struct ELFtypes {
	const char *str;
	int value;
};

static struct ELFtypes elftypes[] = {
	{ "FreeBSD",	ELFOSABI_FREEBSD },
	{ "Linux",	ELFOSABI_LINUX },
	{ "Solaris",	ELFOSABI_SOLARIS },
	{ "SVR4",	ELFOSABI_SYSV },
    { "TesUnix", ELFOSABI_TESTUNIX },
    { "SysV", ELFOSABI_SYSV }
};

int
main(int argc, char **argv)
{
	const char *strtype = "FreeBSD";
	int ch, flags, retval, type;
	bool change, force, listed;

	type = ELFOSABI_FREEBSD;
	retval = 0;
	change = false;
	force = false;
	listed = false;

	while ((ch = getopt(argc, argv, "f:lt:v")) != -1)
		switch (ch) {
		case 'f':
			if (change)
				errx(1, "f option incompatible with t option");
			force = true;
			type = atoi(optarg);
			if (errno == ERANGE || type < 0 || type > 255) {
				warnx("invalid argument to option f: %s",
				    optarg);
				usage();
			}
			break;
		case 'l':
			printelftypes();
			listed = true;
			break;
		case 'v':
			/* does nothing */
			break;
		case 't':
			if (force)
				errx(1, "t option incompatible with f option");
			change = true;
			strtype = optarg;
			break;
		default:
			usage();
	}
	argc -= optind;
	argv += optind;
	if (argc == 0) {
		if (listed)
			exit(0);
		else {
			warnx("no file(s) specified");
			usage();
		}
	}

	if (!force && (type = elftype(strtype)) == -1) {
		warnx("invalid ELF type '%s'", strtype);
		printelftypes();
		usage();
	}

	flags = change || force ? O_RDWR : O_RDONLY;
    // Removed Capsicum/Casper logic

	while (argc != 0) {
		int fd;
		char buffer[EI_NIDENT];

		if ((fd = open(argv[0], flags)) < 0) {
			warn("error opening file %s", argv[0]);
			retval = 1;
			goto fail;
		}
		if (read(fd, buffer, EI_NIDENT) < EI_NIDENT) {
			warnx("file '%s' too short", argv[0]);
			retval = 1; // goto fail handles close? No, fail label calls close(fd)
			goto fail;
		}
		if (buffer[0] != ELFMAG0 || buffer[1] != ELFMAG1 ||
		    buffer[2] != ELFMAG2 || buffer[3] != ELFMAG3) {
			warnx("file '%s' is not ELF format", argv[0]);
			retval = 1;
			goto fail;
		}
		if (!change && !force) {
			fprintf(stdout,
				"File '%s' is of brand '%s' (%u).\n",
				argv[0], iselftype(buffer[EI_OSABI]),
				buffer[EI_OSABI]);
			if (!iselftype(type)) {
				warnx("ELF ABI Brand '%u' is unknown",
				      type);
				printelftypes();
			}
		}
		else {
			buffer[EI_OSABI] = type;
			lseek(fd, 0, SEEK_SET);
			if (write(fd, buffer, EI_NIDENT) != EI_NIDENT) {
				warn("error writing %s %d", argv[0], fd);
				retval = 1;
				goto fail;
			}
		}
fail:
		close(fd);
		argc--;
		argv++;
	}

	return (retval);
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: brandelf [-lv] [-f ELF_ABI_number] [-t string] file ...\n");
	exit(1);
}

static const char *
iselftype(int etype)
{
	size_t elfwalk;

	for (elfwalk = 0; elfwalk < nitems(elftypes); elfwalk++)
		if (etype == elftypes[elfwalk].value)
			return (elftypes[elfwalk].str);
	return ("Unknown");
}

static int
elftype(const char *elfstrtype)
{
	size_t elfwalk;

	for (elfwalk = 0; elfwalk < nitems(elftypes); elfwalk++)
		if (strcasecmp(elfstrtype, elftypes[elfwalk].str) == 0)
			return (elftypes[elfwalk].value);
	return (-1);
}

static void
printelftypes(void)
{
	size_t elfwalk;

	fprintf(stderr, "known ELF types are: ");
	for (elfwalk = 0; elfwalk < nitems(elftypes); elfwalk++)
		fprintf(stderr, "%s(%u) ", elftypes[elfwalk].str,
		    elftypes[elfwalk].value);
	fprintf(stderr, "\n");
}
