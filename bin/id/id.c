#include <sys/types.h>
#include <sys/param.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_GROUPS 16384

typedef enum {
	MODE_DEFAULT,
	MODE_U,
	MODE_G,
	MODE_CAP_G,
	MODE_P,
	MODE_CAP_P,
	MODE_C,
	MODE_D,
	MODE_S,
	MODE_A,
	MODE_M,
	MODE_R,
	MODE_CAP_Z
} id_mode_t;

struct id_opts {
	const char *progname;
	id_mode_t   mode;
	bool        use_name;
	bool        use_real;
	bool        zero_term;
};

static void usage(FILE *f, const char *progname)
{
	fprintf(f, "Usage: %s [OPTION]... [USER]...\n", progname);
}

static void print_version(void)
{
	printf("id (Substrate) 1.0\n");
}

static void print_help(struct id_opts *o)
{
	usage(stdout, o->progname);
	printf(
"Print user and group information for the specified USER,\n"
"or (when USER omitted) for the current user.\n\n"
"  -u, --user           print only the effective user ID\n"
"  -g, --group          print only the effective group ID\n"
"  -G, --groups         print all group IDs\n"
"  -n, --name           print a name instead of a number, for -ugG\n"
"  -r, --real           print the real ID instead of the effective ID, with -ug\n"
"  -z, --zero           delimit output with NUL characters, not whitespace\n"
"  -Z, --context        print only the security context of the process\n"
"  -p                   make the output human-readable (BSD)\n"
"  -P                   output a passwd(5)-style entry (FreeBSD)\n"
"  -c                   output the login class (BSD)\n"
"  -d                   output the home directory (FreeBSD)\n"
"  -s                   output the login shell (FreeBSD)\n"
"  -A                   output the process audit user ID (FreeBSD)\n"
"  -M                   output the MAC label of the process (FreeBSD)\n"
"  -R                   output the routing table (OpenBSD)\n"
"      --help           display this help and exit\n"
"      --version        output version information and exit\n"
	);
}

static void print_id_name(uid_t id, bool is_group, bool use_name)
{
	if(use_name) {
		if(is_group) {
			struct group *gr = getgrgid(id);
			if(gr) {
				fputs(gr->gr_name, stdout);
				return;
			}
		} else {
			struct passwd *pw = getpwuid(id);
			if(pw) {
				fputs(pw->pw_name, stdout);
				return;
			}
		}
	}
	printf("%u", id);
}

static void print_default(uid_t ruid, uid_t euid, gid_t rgid, gid_t egid, int ngroups, gid_t *groups)
{
	struct passwd *pw;
	struct group *gr;

	/* uid=%u(%s) */
	printf("uid=%u", ruid);
	if((pw = getpwuid(ruid))) printf("(%s)", pw->pw_name);

	/* gid=%u(%s) */
	printf(" gid=%u", rgid);
	if((gr = getgrgid(rgid))) printf("(%s)", gr->gr_name);

	/* euid=%u(%s) */
	if(euid != ruid) {
		printf(" euid=%u", euid);
		if((pw = getpwuid(euid))) printf("(%s)", pw->pw_name);
	}

	/* egid=%u(%s) */
	if(egid != rgid) {
		printf(" egid=%u", egid);
		if((gr = getgrgid(egid))) printf("(%s)", gr->gr_name);
	}

	/* groups=%u(%s)... */
	gid_t dgroups[MAX_GROUPS + 2];
	int dcnt = 0;
	
	/* standard id puts egid then rgid then others */
	dgroups[dcnt++] = egid;
	if (rgid != egid) dgroups[dcnt++] = rgid;

	for(int i = 0; i < ngroups && dcnt < MAX_GROUPS + 2; i++) {
		bool found = false;
		for(int j = 0; j < dcnt; j++) {
			if(dgroups[j] == groups[i]) { found = true; break; }
		}
		if(!found) dgroups[dcnt++] = groups[i];
	}

	if(dcnt > 0) {
		printf(" groups=");
		for(int i = 0; i < dcnt; i++) {
			if(i > 0) putchar(',');
			printf("%u", dgroups[i]);
			if((gr = getgrgid(dgroups[i]))) printf("(%s)", gr->gr_name);
		}
	}
}

static void print_bsd_p(const char *req_user, uid_t ruid, uid_t euid, gid_t rgid, int ngroups, gid_t *groups)
{
	struct passwd *pw;
	struct group *gr;

	/* login name */
	const char *login = req_user;
	if(!login) {
		login = getlogin();
		if(!login) {
			pw = getpwuid(ruid);
			if(pw) login = pw->pw_name;
		}
	}
	if(login) printf("login\t%s\n", login);

	/* uid */
	printf("uid\t");
	pw = getpwuid(ruid);
	if(pw) printf("%s\n", pw->pw_name); else printf("%u\n", ruid);

	/* euid */
	if(euid != ruid) {
		printf("euid\t");
		pw = getpwuid(euid);
		if(pw) printf("%s\n", pw->pw_name); else printf("%u\n", euid);
	}

	/* rgid */
	printf("rgid\t");
	gr = getgrgid(rgid);
	if(gr) printf("%s\n", gr->gr_name); else printf("%u\n", rgid);

	/* groups */
	if(ngroups > 0) {
		printf("groups\t");
		for(int i = 0; i < ngroups; i++) {
			if(i > 0) putchar('\t');
			gr = getgrgid(groups[i]);
			if(gr) fputs(gr->gr_name, stdout); else printf("%u", groups[i]);
		}
		putchar('\n');
	}

	/* class omitted as we don't have login classes in substrate yet */
}

static int do_id(const char *user, struct id_opts *o, bool is_last_user)
{
	uid_t ruid, euid;
	gid_t rgid, egid;
	int ngroups;
	gid_t groups[MAX_GROUPS];
	struct passwd *pw = NULL;
	char delim = o->zero_term ? '\0' : '\n';

	if(user) {
		pw = getpwnam(user);
		if(!pw) {
			fprintf(stderr, "%s: '%s': no such user\n", o->progname, user);
			return 1;
		}
		ruid = euid = pw->pw_uid;
		rgid = egid = pw->pw_gid;
		ngroups = MAX_GROUPS;
		/* Use getgrouplist. If it fails due to size, ngroups is updated, but we provided MAX_GROUPS */
		if(getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) < 0) {
			/* truncated, but we got ngroups */
		}
	} else {
		ruid = getuid();
		euid = geteuid();
		rgid = getgid();
		egid = getegid();
		ngroups = getgroups(MAX_GROUPS, groups);
		if(ngroups < 0) ngroups = 0;
	}

	switch(o->mode) {
	case MODE_U:
		print_id_name(o->use_real ? ruid : euid, false, o->use_name);
		putchar(delim);
		break;
	case MODE_G:
		print_id_name(o->use_real ? rgid : egid, true, o->use_name);
		putchar(delim);
		break;
	case MODE_CAP_G: {
		char gdelim = o->zero_term ? '\0' : ' ';
		bool first = true;
		/* Print effective/real if missing from supplementary groups? POSIX says print all distinct. */
		gid_t dgroups[MAX_GROUPS + 2];
		int dcnt = 0;
		dgroups[dcnt++] = o->use_real ? rgid : egid;

		/* Add real/effective if not already present. Wait, POSIX says:
		 * "all different group IDs (effective, real, then supplementary)" */
		bool have_ruid = false;
		for(int i=0; i<dcnt; i++) if(dgroups[i] == rgid) { have_ruid = true; break; }
		if(!have_ruid) dgroups[dcnt++] = rgid;

		bool have_egid = false;
		for(int i=0; i<dcnt; i++) if(dgroups[i] == egid) { have_egid = true; break; }
		if(!have_egid) dgroups[dcnt++] = egid;

		for(int i = 0; i < ngroups && dcnt < MAX_GROUPS + 2; i++) {
			bool found = false;
			for(int j = 0; j < dcnt; j++) {
				if(dgroups[j] == groups[i]) { found = true; break; }
			}
			if(!found) dgroups[dcnt++] = groups[i];
		}

		for(int i = 0; i < dcnt; i++) {
			if(!first) putchar(gdelim);
			print_id_name(dgroups[i], true, o->use_name);
			first = false;
		}
		putchar(delim);
		if(o->zero_term && !is_last_user) putchar('\0');
		break;
	}
	case MODE_DEFAULT:
		print_default(ruid, euid, rgid, egid, ngroups, groups);
		putchar(delim);
		break;
	case MODE_P:
		print_bsd_p(user, ruid, euid, rgid, ngroups, groups);
		break;
	case MODE_CAP_P:
		if(!pw && !user) pw = getpwuid(ruid);
		if(!pw) {
			fprintf(stderr, "%s: cannot find password entry\n", o->progname);
			return 1;
		}
		/* format: name:passwd:uid:gid:class:change:expire:gecos:dir:shell */
		printf("%s:%s:%u:%u::0:0:%s:%s:%s\n",
		       pw->pw_name, pw->pw_passwd, pw->pw_uid, pw->pw_gid,
		       pw->pw_gecos ? pw->pw_gecos : "",
		       pw->pw_dir ? pw->pw_dir : "",
		       pw->pw_shell ? pw->pw_shell : "");
		break;
	case MODE_C:
		/* Login class */
		printf("default\n");
		break;
	case MODE_D:
		if(!pw && !user) pw = getpwuid(ruid);
		if(pw) { fputs(pw->pw_dir, stdout); putchar(delim); }
		break;
	case MODE_S:
		if(!pw && !user) pw = getpwuid(ruid);
		if(pw) { fputs(pw->pw_shell, stdout); putchar(delim); }
		break;
	case MODE_A:
	case MODE_M:
	case MODE_R:
	case MODE_CAP_Z:
		fprintf(stderr, "%s: option unsupported on this platform\n", o->progname);
		return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct id_opts o = {0};
	o.progname = argv[0];

	static const struct option longopts[] = {
		{"user",    no_argument, NULL, 'u'},
		{"group",   no_argument, NULL, 'g'},
		{"groups",  no_argument, NULL, 'G'},
		{"name",    no_argument, NULL, 'n'},
		{"real",    no_argument, NULL, 'r'},
		{"context", no_argument, NULL, 'Z'},
		{"zero",    no_argument, NULL, 'z'},
		{"help",    no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};

	int opt;
    int mode_count = 0;

#define SET_MODE(m) do { o.mode = (m); mode_count++; } while(0)

	while((opt = getopt_long(argc, argv, "uGgnrpPcdAsMaZRz", longopts, NULL)) != -1) {
		switch(opt) {
		case 'u': SET_MODE(MODE_U); break;
		case 'g': SET_MODE(MODE_G); break;
		case 'G': SET_MODE(MODE_CAP_G); break;
		case 'p': SET_MODE(MODE_P); break;
		case 'P': SET_MODE(MODE_CAP_P); break;
		case 'c': SET_MODE(MODE_C); break;
		case 'd': SET_MODE(MODE_D); break;
		case 's': SET_MODE(MODE_S); break;
		case 'A': SET_MODE(MODE_A); break;
		case 'M': SET_MODE(MODE_M); break;
		case 'R': SET_MODE(MODE_R); break;
		case 'Z': SET_MODE(MODE_CAP_Z); break;
		case 'n': o.use_name = true; break;
		case 'r': o.use_real = true; break;
		case 'z': o.zero_term = true; break;
		case 'a': /* ignored */ break;
		case 'h': print_help(&o); return 0;
		case 'V': print_version(); return 0;
		default:
			usage(stderr, o.progname);
			return 1;
		}
	}

	/* Validations */
	if(mode_count > 1) {
		fprintf(stderr, "%s: cannot print multiple formats\n", o.progname);
		return 1;
	}
	if(o.mode == MODE_DEFAULT && o.use_name) {
		fprintf(stderr, "%s: cannot print only names or real IDs in default format\n", o.progname);
		return 1;
	}
	if(o.mode == MODE_DEFAULT && o.use_real) {
		/* -r is ignored in default mode by BSD, but GNU rejects it.
		 * "If combined with -G, BSD-first: accept but ignore"
		 * Let's just follow BSD's lead and potentially ignore.
		 * POSIX: -r with -u or -g. */
	}
	if(o.mode == MODE_DEFAULT && o.zero_term) {
		fprintf(stderr, "%s: option --zero not permitted in default format\n", o.progname);
		return 1;
	}

	int ret = 0;
	if(optind == argc) {
		ret = do_id(NULL, &o, true);
	} else {
		for(int i = optind; i < argc; i++) {
			if(do_id(argv[i], &o, (i == argc - 1)) != 0) {
				ret = 1;
			}
		}
	}

	return ret;
}
