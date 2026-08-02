/*
 * renice -- alter the scheduling priority (nice value) of running processes.
 *
 *   renice [-n] priority [[-p] pid ...] [-g pgrp ...] [-u user ...]
 *
 * `priority' is an absolute nice value in the range -20 (most favourable) to
 * 19 (least favourable); it may be introduced by -n for compatibility.  The
 * -p, -g and -u flags select whether the operands that follow are process IDs
 * (the default), process-group IDs, or users (name or numeric uid).  Only the
 * super-user may lower a nice value or renice another user's processes; the
 * kernel enforces this and renice reports the resulting EPERM.
 */
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/resource.h>

static const char *which_name(int which)
{
	switch (which) {
	case PRIO_PGRP: return "process group";
	case PRIO_USER: return "user";
	default:        return "process";
	}
}

/* Resolve a -u operand (user name or numeric uid) to an id. */
static int resolve_user(const char *s, id_t *out)
{
	struct passwd *pw = getpwnam(s);
	if (pw) {
		*out = pw->pw_uid;
		return 0;
	}
	char *end;
	long  v;
	errno = 0;
	v = strtol(s, &end, 10);
	/* Range-check so a huge id can't truncate into a valid id_t and
	 * target the wrong user (RENICE-01/03). */
	if (end != s && *end == '\0' && errno != ERANGE && v >= 0 && v <= INT_MAX) {
		*out = (id_t)v;
		return 0;
	}
	return -1;
}

static void usage(void)
{
	fprintf(stderr,
	    "usage: renice [-n] priority [[-p] pid ...] [-g pgrp ...] [-u user ...]\n");
	exit(2);
}

int main(int argc, char *argv[])
{
	int i = 1;

	if (argc < 3)
		usage();

	/* Priority operand, optionally introduced by -n. */
	if (strcmp(argv[i], "-n") == 0) {
		if (++i >= argc)
			usage();
	}

	char *end;
	long prio = strtol(argv[i], &end, 10);
	if (end == argv[i] || *end != '\0')
		usage();
	i++;

	/* Clamp to the conventional nice range so an out-of-range request
	 * becomes a boundary value rather than an EINVAL. */
	if (prio < PRIO_MIN)
		prio = PRIO_MIN;
	if (prio > PRIO_MAX - 1)
		prio = PRIO_MAX - 1;

	if (i >= argc)
		usage();   /* need at least one target */

	int which = PRIO_PROCESS;
	int rc = 0;

	for (; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0) { which = PRIO_PROCESS; continue; }
		if (strcmp(argv[i], "-g") == 0) { which = PRIO_PGRP;    continue; }
		if (strcmp(argv[i], "-u") == 0) { which = PRIO_USER;    continue; }

		id_t who;
		if (which == PRIO_USER) {
			if (resolve_user(argv[i], &who) != 0) {
				fprintf(stderr, "renice: %s: unknown user\n", argv[i]);
				rc = 1;
				continue;
			}
		} else {
			errno = 0;
			long idv = strtol(argv[i], &end, 10);
			if (end == argv[i] || *end != '\0' || errno == ERANGE ||
			    idv < 0 || idv > INT_MAX) {
				fprintf(stderr, "renice: %s: invalid %s id\n",
				    argv[i], which_name(which));
				rc = 1;
				continue;
			}
			who = (id_t)idv;
		}

		/* getpriority() returns -1 on error, but -1 is also a valid nice
		 * value; disambiguate via errno. */
		errno = 0;
		int old = getpriority(which, who);
		int have_old = !(old == -1 && errno != 0);

		if (setpriority(which, who, (int)prio) != 0) {
			fprintf(stderr, "renice: failed to set priority for %s %s: %s\n",
			    which_name(which), argv[i], strerror(errno));
			rc = 1;
			continue;
		}

		if (have_old)
			printf("%ld (%s ID) old priority %d, new priority %ld\n",
			    (long)who, which_name(which), old, prio);
		else
			printf("%ld (%s ID) new priority %ld\n",
			    (long)who, which_name(which), prio);
	}

	return rc;
}
