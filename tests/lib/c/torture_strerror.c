/*
 * torture_strerror.c -- strerror() has a message for every <errno.h> value.
 *
 * strerror(ELOOP) used to be "Unknown error", as did the streams, IPC and
 * quota codes, so perror() and err() printed nothing useful for them.
 * Every value <errno.h> defines must map to its own message, and
 * strerror_r() must agree with strerror().
 *
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define E(x) { x, #x }

static const struct {
	int		 num;
	const char	*name;
} codes[] = {
	E(EPERM), E(ENOENT), E(ESRCH), E(EINTR), E(EIO), E(ENXIO), E(E2BIG),
	E(ENOEXEC), E(EBADF), E(ECHILD), E(EAGAIN), E(ENOMEM), E(EACCES),
	E(EFAULT), E(ENOTBLK), E(EBUSY), E(EEXIST), E(EXDEV), E(ENODEV),
	E(ENOTDIR), E(EISDIR), E(EINVAL), E(ENFILE), E(EMFILE), E(ENOTTY),
	E(ETXTBSY), E(EFBIG), E(ENOSPC), E(ESPIPE), E(EROFS), E(EMLINK),
	E(EPIPE), E(EDOM), E(ERANGE), E(ENOSYS), E(ENOTEMPTY), E(EDEADLK),
	E(ENAMETOOLONG), E(ELOOP), E(EOWNERDEAD), E(ENOTRECOVERABLE),
	E(ETIMEDOUT), E(EOVERFLOW), E(EUNKNOWNFS), E(EILSEQ), E(EBADMSG),
	E(EIDRM), E(EMULTIHOP), E(ENODATA), E(ENOLINK), E(ENOMSG), E(ENOSR),
	E(ENOSTR), E(EPROTO), E(ETIME), E(EINPROGRESS), E(EALREADY),
	E(ENOTSOCK), E(EDESTADDRREQ), E(EMSGSIZE), E(EPROTOTYPE),
	E(ENOPROTOOPT), E(EPROTONOSUPPORT), E(ESOCKTNOSUPPORT), E(EOPNOTSUPP),
	E(ENOLCK), E(EPFNOSUPPORT), E(EAFNOSUPPORT), E(EADDRINUSE),
	E(EADDRNOTAVAIL), E(ENETDOWN), E(ENETUNREACH), E(ENETRESET),
	E(ECONNABORTED), E(ECONNRESET), E(ENOBUFS), E(EISCONN), E(ENOTCONN),
	E(ESHUTDOWN), E(ETOOMANYREFS), E(ECONNREFUSED), E(EHOSTDOWN),
	E(EHOSTUNREACH), E(ECANCELED), E(EDQUOT), E(ESTALE),
};

#define NCODES	(sizeof(codes) / sizeof(codes[0]))

int
main(void)
{
	char buf[256];
	const char *unknown = strerror(-1);
	size_t i, j;
	int failures = 0;

	for (i = 0; i < NCODES; i++) {
		const char *msg = strerror(codes[i].num);

		if (msg == NULL || *msg == '\0' || strcmp(msg, unknown) == 0) {
			printf("FAIL: strerror(%s = %d) = \"%s\"\n",
			    codes[i].name, codes[i].num, msg ? msg : "(null)");
			failures++;
			continue;
		}
		if (strerror_r(codes[i].num, buf, sizeof(buf)) != 0 ||
		    strcmp(buf, msg) != 0) {
			printf("FAIL: strerror_r(%s) = \"%s\", strerror = \"%s\"\n",
			    codes[i].name, buf, msg);
			failures++;
		}
		/* Distinct numbers need distinct messages. */
		for (j = 0; j < i; j++) {
			if (codes[j].num != codes[i].num &&
			    strcmp(strerror(codes[j].num), msg) == 0) {
				printf("FAIL: %s and %s share \"%s\"\n",
				    codes[j].name, codes[i].name, msg);
				failures++;
			}
		}
	}

	printf("Result: %s (%zu codes, %d failures)\n",
	    failures ? "FAIL" : "PASS", NCODES, failures);
	return failures != 0;
}
