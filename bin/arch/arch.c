/*
 * arch - print the machine hardware architecture.
 *
 * Equivalent to `uname -m`: writes the machine field of uname(2) followed
 * by a newline.  On substrate the kernel reports "i386".  The only options
 * accepted are the informational --help / --version; arch takes no operands.
 */
#include <stdio.h>
#include <string.h>

#include <sys/utsname.h>

static void usage(FILE *out, const char *prog)
{
	fprintf(out, "Usage: %s\n", prog);
	fprintf(out, "Print machine architecture.\n\n");
	fprintf(out, "      --help     display this help and exit\n");
	fprintf(out, "      --version  output version information and exit\n");
}

int main(int argc, char *argv[])
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0) {
			usage(stdout, argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--version") == 0) {
			printf("arch (substrate)\n");
			return 0;
		}
		fprintf(stderr, "arch: unrecognized argument '%s'\n", argv[i]);
		usage(stderr, argv[0]);
		return 1;
	}

	struct utsname u;
	if (uname(&u) == -1) {
		perror("arch");
		return 1;
	}
	printf("%s\n", u.machine);
	return 0;
}
