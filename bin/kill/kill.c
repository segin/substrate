/*
 * kill - send a signal to processes.
 *
 *   kill [-s signal | -signal] pid...
 *   kill -l [exit_status]
 *
 * Accepts a signal by number (-9), by name (-KILL / -SIGKILL), or via
 * -s NAME/NUM.  pid operands are parsed strictly (a bad or out-of-range
 * pid is diagnosed, not silently turned into pid 0 which would target the
 * whole process group).  A negative pid targets a process group, and pid 0
 * targets the caller's process group, both per POSIX.  Exit status is
 * nonzero if any target could not be signalled or an argument was invalid.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct signame {
	const char *name;	/* without the "SIG" prefix */
	int num;
};

static const struct signame signals[] = {
#ifdef SIGHUP
	{ "HUP", SIGHUP },
#endif
#ifdef SIGINT
	{ "INT", SIGINT },
#endif
#ifdef SIGQUIT
	{ "QUIT", SIGQUIT },
#endif
#ifdef SIGILL
	{ "ILL", SIGILL },
#endif
#ifdef SIGTRAP
	{ "TRAP", SIGTRAP },
#endif
#ifdef SIGABRT
	{ "ABRT", SIGABRT },
#endif
#ifdef SIGIOT
	{ "IOT", SIGIOT },
#endif
#ifdef SIGBUS
	{ "BUS", SIGBUS },
#endif
#ifdef SIGFPE
	{ "FPE", SIGFPE },
#endif
#ifdef SIGKILL
	{ "KILL", SIGKILL },
#endif
#ifdef SIGUSR1
	{ "USR1", SIGUSR1 },
#endif
#ifdef SIGSEGV
	{ "SEGV", SIGSEGV },
#endif
#ifdef SIGUSR2
	{ "USR2", SIGUSR2 },
#endif
#ifdef SIGPIPE
	{ "PIPE", SIGPIPE },
#endif
#ifdef SIGALRM
	{ "ALRM", SIGALRM },
#endif
#ifdef SIGTERM
	{ "TERM", SIGTERM },
#endif
#ifdef SIGSTKFLT
	{ "STKFLT", SIGSTKFLT },
#endif
#ifdef SIGCHLD
	{ "CHLD", SIGCHLD },
#endif
#ifdef SIGCONT
	{ "CONT", SIGCONT },
#endif
#ifdef SIGSTOP
	{ "STOP", SIGSTOP },
#endif
#ifdef SIGTSTP
	{ "TSTP", SIGTSTP },
#endif
#ifdef SIGTTIN
	{ "TTIN", SIGTTIN },
#endif
#ifdef SIGTTOU
	{ "TTOU", SIGTTOU },
#endif
#ifdef SIGURG
	{ "URG", SIGURG },
#endif
#ifdef SIGXCPU
	{ "XCPU", SIGXCPU },
#endif
#ifdef SIGXFSZ
	{ "XFSZ", SIGXFSZ },
#endif
#ifdef SIGVTALRM
	{ "VTALRM", SIGVTALRM },
#endif
#ifdef SIGPROF
	{ "PROF", SIGPROF },
#endif
#ifdef SIGWINCH
	{ "WINCH", SIGWINCH },
#endif
#ifdef SIGIO
	{ "IO", SIGIO },
#endif
#ifdef SIGPOLL
	{ "POLL", SIGPOLL },
#endif
#ifdef SIGPWR
	{ "PWR", SIGPWR },
#endif
#ifdef SIGSYS
	{ "SYS", SIGSYS },
#endif
};

static const size_t nsignals = sizeof(signals) / sizeof(signals[0]);

static const char *prog = "kill";

static void usage(FILE *out)
{
	fprintf(out, "usage: %s [-s signal | -signal] pid...\n", prog);
	fprintf(out, "       %s -l [exit_status]\n", prog);
}

/* Parse a whole-string signed integer; return 0 on success. */
static int parse_long(const char *s, long *out)
{
	char *end;
	errno = 0;
	long v = strtol(s, &end, 10);
	if (end == s || *end != '\0' || errno == ERANGE)
		return -1;
	*out = v;
	return 0;
}

/* Resolve a signal spec (name with/without SIG prefix, or a number).
 * Returns the signal number, or -1 if unknown. */
static int signal_from_spec(const char *spec)
{
	if (isdigit((unsigned char)spec[0])) {
		long v;
		if (parse_long(spec, &v) != 0 || v < 0 || v >= NSIG)
			return -1;
		return (int)v;
	}
	if (strncasecmp(spec, "SIG", 3) == 0)
		spec += 3;
	for (size_t i = 0; i < nsignals; i++) {
		if (strcasecmp(spec, signals[i].name) == 0)
			return signals[i].num;
	}
	return -1;
}

static const char *name_from_signal(int num)
{
	for (size_t i = 0; i < nsignals; i++) {
		if (signals[i].num == num)
			return signals[i].name;
	}
	return NULL;
}

static int do_list(int argc, char **argv, int idx)
{
	if (idx >= argc) {
		/* List every known signal name. */
		for (size_t i = 0; i < nsignals; i++)
			printf("%s%c", signals[i].name,
			    (i + 1 == nsignals) ? '\n' : ' ');
		return 0;
	}
	/* Decode each argument: a number that may be an exit status
	 * (128+signo) or a bare signal number, printing the name. */
	int rc = 0;
	for (; idx < argc; idx++) {
		long v;
		if (parse_long(argv[idx], &v) != 0 || v < 0) {
			int num = signal_from_spec(argv[idx]);
			if (num < 0) {
				fprintf(stderr, "%s: %s: invalid signal specification\n",
				    prog, argv[idx]);
				rc = 1;
				continue;
			}
			printf("%d\n", num);
			continue;
		}
		if (v > 128)
			v -= 128;
		const char *nm = name_from_signal((int)v);
		if (nm)
			printf("%s\n", nm);
		else {
			fprintf(stderr, "%s: %ld: invalid signal number\n", prog, v);
			rc = 1;
		}
	}
	return rc;
}

int main(int argc, char *argv[])
{
	if (argv[0] && argv[0][0])
		prog = argv[0];

	if (argc < 2) {
		usage(stderr);
		return 1;
	}

	int sig = SIGTERM;
	int i = 1;

	if (argv[1][0] == '-' && argv[1][1] != '\0') {
		const char *opt = argv[1] + 1;
		if (strcmp(opt, "l") == 0 || strcmp(opt, "-list") == 0)
			return do_list(argc, argv, 2);
		if (strcmp(opt, "s") == 0 || strcmp(opt, "-signal") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "%s: option requires a signal\n", prog);
				usage(stderr);
				return 1;
			}
			sig = signal_from_spec(argv[i + 1]);
			if (sig < 0) {
				fprintf(stderr, "%s: %s: invalid signal specification\n",
				    prog, argv[i + 1]);
				return 1;
			}
			i += 2;
		} else if (strcmp(opt, "-") == 0) {	/* "--" ends options */
			i = 2;
		} else {
			/* -signal (name or number) */
			sig = signal_from_spec(opt);
			if (sig < 0) {
				fprintf(stderr, "%s: %s: invalid signal specification\n",
				    prog, opt);
				return 1;
			}
			i = 2;
		}
	}

	if (i >= argc) {
		fprintf(stderr, "%s: no process specified\n", prog);
		usage(stderr);
		return 1;
	}

	int rc = 0;
	for (; i < argc; i++) {
		long v;
		if (parse_long(argv[i], &v) != 0 ||
		    v < (long)INT_MIN || v > (long)INT_MAX) {
			fprintf(stderr, "%s: %s: arguments must be process or job IDs\n",
			    prog, argv[i]);
			rc = 1;
			continue;
		}
		if (kill((pid_t)v, sig) < 0) {
			fprintf(stderr, "%s: (%ld): %s\n", prog, v, strerror(errno));
			rc = 1;
		}
	}
	return rc;
}
