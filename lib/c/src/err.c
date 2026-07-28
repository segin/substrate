/*
 * <err.h> implementation — BSD-style diagnostic helpers.
 *
 * err / errx / warn / warnx (+ va_list verr/verrx/vwarn/vwarnx).
 * Output to stderr, prefixed with the program name from
 * __progname / argv[0].
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <err.h>

/* Weak so crt0 or the program itself can override; default keeps
 * err output sensible even if argv[0] capture isn't wired up. */
const char *__progname __attribute__((weak)) = "";

static void
print_prefix(void)
{
	const char *p = __progname;
	if (p && *p)
		fprintf(stderr, "%s: ", p);
}

void
vwarnx(const char *fmt, va_list ap)
{
	print_prefix();
	if (fmt)
		vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
}

void
warnx(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}

void
vwarn(const char *fmt, va_list ap)
{
	int saved = errno;
	print_prefix();
	if (fmt) {
		vfprintf(stderr, fmt, ap);
		fputs(": ", stderr);
	}
	fputs(strerror(saved), stderr);
	fputc('\n', stderr);
}

void
warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
}

void
verrx(int eval, const char *fmt, va_list ap)
{
	vwarnx(fmt, ap);
	exit(eval);
}

void
errx(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verrx(eval, fmt, ap);
	/* NOTREACHED */
	va_end(ap);
}

void
verr(int eval, const char *fmt, va_list ap)
{
	vwarn(fmt, ap);
	exit(eval);
}

void
err(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verr(eval, fmt, ap);
	/* NOTREACHED */
	va_end(ap);
}
