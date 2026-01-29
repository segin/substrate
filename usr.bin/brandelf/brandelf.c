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

#include <sys/types.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <elf.h>
#include <stdarg.h>
#include <stdbool.h>

/* Fallback definitions for non-native builds */
#ifndef ELFOSABI_SYSV
#define	ELFOSABI_SYSV		0
#endif
#ifndef ELFOSABI_LINUX
#define	ELFOSABI_LINUX		3
#endif
#ifndef ELFOSABI_FREEBSD
#define	ELFOSABI_FREEBSD	9
#endif
#ifndef ELFOSABI_SOLARIS
#define	ELFOSABI_SOLARIS	6
#endif

/* Substrate Definitions */
#ifndef ELFOSABI_SUBSTRATE
#define ELFOSABI_SUBSTRATE	64	/* Generic Substrate (Temporary) */
#endif

#define	PO_SIG	0	/* EI_MAG0 */
#define	PO_ABI	7	/* EI_OSABI */

static void usage(void);
static void printelftypes(void);

/* Portable err/warn implementation */
static const char *progname;

static void
vwarnc(int code, const char *fmt, va_list ap)
{
	fprintf(stderr, "%s: ", progname);
	if (fmt != NULL) {
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, ": ");
	}
	fprintf(stderr, "%s\n", strerror(code));
}

static void
vwarnx(const char *fmt, va_list ap)
{
	fprintf(stderr, "%s: ", progname);
	if (fmt != NULL)
		vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}



static void
errx(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
	exit(eval);
}

static void
warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarnc(errno, fmt, ap);
	va_end(ap);
}

static void
warnx(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}

struct elftype {
	const char *name;
	int value;
};

static struct elftype elftypes[] = {
	{ "FreeBSD",	ELFOSABI_FREEBSD },
	{ "Linux",	ELFOSABI_LINUX },
	{ "Solaris",	ELFOSABI_SOLARIS },
	{ "SVR4",	ELFOSABI_SYSV },
	{ "Substrate",  ELFOSABI_SUBSTRATE },
	{ "SysV",	ELFOSABI_SYSV },
	{ NULL,		-1 }
};

static int
get_elf_type(const char *name)
{
	struct elftype *et;

	for (et = elftypes; et->name != NULL; et++) {
		if (strcasecmp(name, et->name) == 0)
			return (et->value);
	}
	return (-1);
}

static const char *
get_elf_name(int value)
{
	struct elftype *et;

	for (et = elftypes; et->name != NULL; et++) {
		if (et->value == value)
			return (et->name);
	}
	return ("Unknown");
}

int
main(int argc, char *argv[])
{
	const char *type_name = "Substrate";
	int ch, fd, type;
	int retval = 0;
	bool change = false;
	bool force = false;
	bool list = false;
	char buffer[EI_NIDENT];

	progname = argv[0];
	if ((progname = strrchr(progname, '/')) != NULL)
		progname++;
	else
		progname = argv[0];

	type = ELFOSABI_SUBSTRATE;

	while ((ch = getopt(argc, argv, "f:lt:v")) != -1) {
		switch (ch) {
		case 'f':
			if (change)
				errx(1, "-f option is incompatible with -t");
			force = true;
			type = atoi(optarg);
			if (type < 0 || type > 255)
				errx(1, "invalid argument to -f: %s", optarg);
			break;
		case 'l':
			list = true;
			break;
		case 't':
			if (force)
				errx(1, "-t option is incompatible with -f");
			change = true;
			type_name = optarg;
			break;
		case 'v':
			/* Verbose ignored for compatibility */
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (list) {
		printelftypes();
		return (0);
	}

	if (argc == 0) {
		warnx("no files specified");
		usage();
	}

	if (change) {
		type = get_elf_type(type_name);
		if (type == -1) {
			warnx("invalid ELF type '%s'", type_name);
			printelftypes();
			return (1);
		}
	}

	for (; *argv != NULL; argv++) {
		if ((fd = open(*argv, change || force ? O_RDWR : O_RDONLY)) < 0) {
			warn("cannot open %s", *argv);
			retval = 1;
			continue;
		}

		if (read(fd, buffer, sizeof(buffer)) != sizeof(buffer) ||
		    buffer[EI_MAG0] != ELFMAG0 || buffer[EI_MAG1] != ELFMAG1 ||
		    buffer[EI_MAG2] != ELFMAG2 || buffer[EI_MAG3] != ELFMAG3) {
			warnx("%s: not an ELF file", *argv);
			retval = 1;
			close(fd);
			continue;
		}

		if (!change && !force) {
			printf("File '%s' is of brand '%s' (%u).\n",
			    *argv, get_elf_name(buffer[EI_OSABI]),
			    buffer[EI_OSABI]);
		} else {
			buffer[EI_OSABI] = type;
			if (lseek(fd, 0, SEEK_SET) == -1 ||
			    write(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
				warn("%s: failed to write ELF header", *argv);
				retval = 1;
			}
		}
		close(fd);
	}

	return (retval);
}

static void
usage(void)
{
	fprintf(stderr, "usage: brandelf [-l] [-f abi_numer] [-t string] file ...\n");
	exit(1);
}

static void
printelftypes(void)
{
	struct elftype *et;

	fprintf(stderr, "known ELF types are: ");
	for (et = elftypes; et->name != NULL; et++)
		fprintf(stderr, "%s(%u) ", et->name, et->value);
	fprintf(stderr, "\n");
}
