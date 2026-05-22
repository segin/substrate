/*
 * stty — get and set terminal line settings.
 *
 * Conformance: POSIX.1-2024 stty, plus the GNU coreutils and
 * 4.4BSD/FreeBSD extension sets.  Where GNU and BSD disagree the
 * BSD behaviour is normative (verbose layout, gfmt1 save form,
 * BSD control-character spellings).  Specified by
 * docs/specs/stty.md.
 *
 * Usage:
 *   stty [-a|-e|-g] [-F device | -f device]
 *   stty [-F device | -f device] operand ...
 *
 * With no operands stty reports the settings that differ from
 * "sane".  -a / -e give a full grouped report; -g gives a
 * gfmt1: save token that stty itself reads back.  The operand
 * "size" prints "rows columns"; "speed" prints the line speed.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

static const char *prog = "stty";

static void fatal(const char *msg)
{
	fprintf(stderr, "%s: %s\n", prog, msg);
	exit(1);
}

static void fatal2(const char *msg, const char *arg)
{
	fprintf(stderr, "%s: %s: %s\n", prog, msg, arg);
	exit(1);
}

static void help(void)
{
	fputs(
	    "usage: stty [-a|-e|-g] [-F device | -f device]\n"
	    "       stty [-F device | -f device] operand ...\n"
	    "\n"
	    "  -a, --all     report all settings (grouped)\n"
	    "  -e            report all settings (synonym of -a)\n"
	    "  -g, --save    report settings as a gfmt1: save token\n"
	    "  -F, --file D  operate on device D\n"
	    "  -f D          operate on device D\n"
	    "\n"
	    "Operands set modes ([-]name), control characters\n"
	    "(intr ^C, erase ^?, susp undef), speeds (a number,\n"
	    "ispeed N, ospeed N), window size (rows N, cols N) and\n"
	    "combinations (sane, raw, cooked, cbreak, nl, ek,\n"
	    "evenp, oddp, parity, crt, dec, pass8, litout).\n"
	    "The operands 'size' and 'speed' query and print.\n",
	    stdout);
	exit(0);
}

/* ---- mode-flag tables ---------------------------------------------- */

enum { FI, FO, FC, FL };	/* iflag / oflag / cflag / lflag */

struct flagent {
	const char *name;
	int         grp;
	tcflag_t    mask;
};

/* Canonical boolean modes, printed in verbose/abbreviated reports. */
static const struct flagent flagtab[] = {
	{ "ignbrk",  FI, IGNBRK  }, { "brkint",  FI, BRKINT  },
	{ "ignpar",  FI, IGNPAR  }, { "parmrk",  FI, PARMRK  },
	{ "inpck",   FI, INPCK   }, { "istrip",  FI, ISTRIP  },
	{ "inlcr",   FI, INLCR   }, { "igncr",   FI, IGNCR   },
	{ "icrnl",   FI, ICRNL   }, { "iuclc",   FI, IUCLC   },
	{ "ixon",    FI, IXON    }, { "ixany",   FI, IXANY   },
	{ "ixoff",   FI, IXOFF   }, { "imaxbel", FI, IMAXBEL },
	{ "iutf8",   FI, IUTF8   },

	{ "opost",   FO, OPOST   }, { "olcuc",   FO, OLCUC   },
	{ "onlcr",   FO, ONLCR   }, { "ocrnl",   FO, OCRNL   },
	{ "onocr",   FO, ONOCR   }, { "onlret",  FO, ONLRET  },
	{ "ofill",   FO, OFILL   }, { "ofdel",   FO, OFDEL   },

	{ "cstopb",  FC, CSTOPB  }, { "cread",   FC, CREAD   },
	{ "parenb",  FC, PARENB  }, { "parodd",  FC, PARODD  },
	{ "hupcl",   FC, HUPCL   }, { "clocal",  FC, CLOCAL  },

	{ "isig",    FL, ISIG    }, { "icanon",  FL, ICANON  },
	{ "echo",    FL, ECHO    }, { "echoe",   FL, ECHOE   },
	{ "echok",   FL, ECHOK   }, { "echonl",  FL, ECHONL  },
	{ "noflsh",  FL, NOFLSH  }, { "tostop",  FL, TOSTOP  },
	{ "echoctl", FL, ECHOCTL }, { "echoprt", FL, ECHOPRT },
	{ "echoke",  FL, ECHOKE  }, { "flusho",  FL, FLUSHO  },
	{ "pendin",  FL, PENDIN  }, { "iexten",  FL, IEXTEN  },
};
#define NFLAGS ((int)(sizeof(flagtab) / sizeof(flagtab[0])))

/* Accepted-only synonyms — not printed in reports. */
static const struct flagent aliastab[] = {
	{ "hup",      FC, HUPCL   },	/* BSD synonym of hupcl   */
	{ "tandem",   FI, IXOFF   },	/* BSD synonym of ixoff   */
	{ "crterase", FL, ECHOE   },	/* GNU readability alias  */
	{ "crtkill",  FL, ECHOKE  },
	{ "ctlecho",  FL, ECHOCTL },
	{ "prterase", FL, ECHOPRT },
};
#define NALIAS ((int)(sizeof(aliastab) / sizeof(aliastab[0])))

struct ccent {
	const char *name;
	int         idx;
};

/* Canonical control characters (BSD spellings preferred). */
static const struct ccent cctab[] = {
	{ "intr",  VINTR  }, { "quit",  VQUIT    }, { "erase", VERASE },
	{ "kill",  VKILL  }, { "eof",   VEOF     }, { "eol",   VEOL   },
	{ "eol2",  VEOL2  }, { "swtch", VSWTC    }, { "start", VSTART },
	{ "stop",  VSTOP  }, { "susp",  VSUSP    }, { "reprint", VREPRINT },
	{ "werase",VWERASE}, { "lnext", VLNEXT   }, { "discard", VDISCARD },
};
#define NCC ((int)(sizeof(cctab) / sizeof(cctab[0])))

/* Accepted-only control-character synonyms. */
static const struct ccent ccaliastab[] = {
	{ "rprnt", VREPRINT },		/* GNU synonym of reprint */
	{ "flush", VDISCARD },		/* GNU synonym of discard */
};
#define NCCALIAS ((int)(sizeof(ccaliastab) / sizeof(ccaliastab[0])))

/* gfmt1 save-token control-character keys. */
static const struct ccent gfcc[] = {
	{ "discard", VDISCARD }, { "eof",   VEOF   }, { "eol",  VEOL  },
	{ "eol2",    VEOL2    }, { "erase", VERASE }, { "intr", VINTR },
	{ "kill",    VKILL    }, { "lnext", VLNEXT }, { "min",  VMIN  },
	{ "quit",    VQUIT    }, { "reprint", VREPRINT }, { "start", VSTART },
	{ "stop",    VSTOP    }, { "susp",  VSUSP  }, { "swtch", VSWTC },
	{ "time",    VTIME    }, { "werase", VWERASE },
};
#define NGFCC ((int)(sizeof(gfcc) / sizeof(gfcc[0])))

/* ---- flag-field accessors ------------------------------------------ */

static tcflag_t gf_get(const struct termios *t, int g)
{
	switch (g) {
	case FI: return t->c_iflag;
	case FO: return t->c_oflag;
	case FC: return t->c_cflag;
	default: return t->c_lflag;
	}
}

static void gf_set(struct termios *t, int g, tcflag_t mask, int on)
{
	tcflag_t *p;

	switch (g) {
	case FI: p = &t->c_iflag; break;
	case FO: p = &t->c_oflag; break;
	case FC: p = &t->c_cflag; break;
	default: p = &t->c_lflag; break;
	}
	if (on)
		*p |= mask;
	else
		*p &= ~mask;
}

/* ---- "sane" reference (docs/specs/stty.md §5) ---------------------- */

static void apply_sane(struct termios *t)
{
	t->c_iflag = BRKINT | ICRNL | IMAXBEL | IXON;
	t->c_oflag = OPOST | ONLCR;
	t->c_cflag = (t->c_cflag & ~(CSIZE | PARENB | PARODD | CSTOPB))
	           | CS8 | CREAD;
	t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL
	           | ECHOKE | IEXTEN;
	t->c_cc[VINTR]    = 3;    t->c_cc[VQUIT]    = 28;
	t->c_cc[VERASE]   = 127;  t->c_cc[VKILL]    = 21;
	t->c_cc[VEOF]     = 4;    t->c_cc[VEOL]     = 0;
	t->c_cc[VEOL2]    = 0;    t->c_cc[VSWTC]    = 0;
	t->c_cc[VSTART]   = 17;   t->c_cc[VSTOP]    = 19;
	t->c_cc[VSUSP]    = 26;   t->c_cc[VREPRINT] = 18;
	t->c_cc[VWERASE]  = 23;   t->c_cc[VLNEXT]   = 22;
	t->c_cc[VDISCARD] = 15;
	t->c_cc[VMIN]     = 1;    t->c_cc[VTIME]    = 0;
}

static void apply_raw(struct termios *t, int raw)
{
	if (raw) {
		t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR
		              | IGNCR | ICRNL | IXON);
		t->c_oflag &= ~OPOST;
		t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
		t->c_cflag = (t->c_cflag & ~(CSIZE | PARENB)) | CS8;
		t->c_cc[VMIN]  = 1;
		t->c_cc[VTIME] = 0;
	} else {
		t->c_iflag |= BRKINT | ICRNL | IMAXBEL | IXON;
		t->c_oflag |= OPOST | ONLCR;
		t->c_lflag |= ISIG | ICANON | ECHO | ECHOE | ECHOK
		           | ECHOCTL | ECHOKE | IEXTEN;
	}
}

static void apply_parity(struct termios *t, int enable, int odd)
{
	if (enable) {
		t->c_cflag = (t->c_cflag & ~CSIZE) | CS7 | PARENB;
		if (odd)
			t->c_cflag |= PARODD;
		else
			t->c_cflag &= ~PARODD;
	} else {
		t->c_cflag = (t->c_cflag & ~(CSIZE | PARENB | PARODD)) | CS8;
	}
}

/* ---- token printer (wraps, optional continuation indent) ----------- */

static int         g_col;
static const char *g_cont = "";

static void tok(const char *s)
{
	int n = (int)strlen(s);

	if (g_col && g_col + 1 + n > 80) {
		printf("\n%s", g_cont);
		g_col = (int)strlen(g_cont);
	} else if (g_col) {
		putchar(' ');
		g_col++;
	}
	fputs(s, stdout);
	g_col += n;
}

static void tok_end(void)
{
	if (g_col)
		putchar('\n');
	g_col = 0;
}

static void cc_str(cc_t v, char *buf, size_t sz)
{
	if (v == 0)
		snprintf(buf, sz, "<undef>");
	else if (v == 127)
		snprintf(buf, sz, "^?");
	else if (v < 32)
		snprintf(buf, sz, "^%c", v + 0x40);
	else if (v < 127)
		snprintf(buf, sz, "%c", v);
	else
		snprintf(buf, sz, "0x%02x", (unsigned)v);
}

/* ---- reports ------------------------------------------------------- */

static void emit_flag_group(const struct termios *t, int grp)
{
	char buf[24];
	int i;

	for (i = 0; i < NFLAGS; i++) {
		if (flagtab[i].grp != grp)
			continue;
		if (gf_get(t, grp) & flagtab[i].mask) {
			tok(flagtab[i].name);
		} else {
			snprintf(buf, sizeof buf, "-%s", flagtab[i].name);
			tok(buf);
		}
	}
}

static void show_verbose(const struct termios *t, const struct winsize *ws)
{
	char buf[40], val[16];
	unsigned cs;
	int i;

	if (t->c_ispeed == t->c_ospeed)
		printf("speed %u baud;", t->c_ospeed);
	else
		printf("ispeed %u baud; ospeed %u baud;",
		    t->c_ispeed, t->c_ospeed);
	printf(" %u rows; %u columns; line = %u;\n",
	    ws->ws_row, ws->ws_col, (unsigned)t->c_line);

	fputs("lflags:", stdout);  g_col = 7;  g_cont = "\t";
	emit_flag_group(t, FL);
	tok_end();

	fputs("iflags:", stdout);  g_col = 7;
	emit_flag_group(t, FI);
	tok_end();

	fputs("oflags:", stdout);  g_col = 7;
	emit_flag_group(t, FO);
	tok_end();

	fputs("cflags:", stdout);  g_col = 7;
	cs = t->c_cflag & CSIZE;
	tok(cs == CS5 ? "cs5" : cs == CS6 ? "cs6"
	  : cs == CS7 ? "cs7" : "cs8");
	emit_flag_group(t, FC);
	tok_end();

	fputs("cchars:", stdout);  g_col = 7;
	for (i = 0; i < NCC; i++) {
		cc_str(t->c_cc[cctab[i].idx], val, sizeof val);
		snprintf(buf, sizeof buf, "%s = %s;", cctab[i].name, val);
		tok(buf);
	}
	snprintf(buf, sizeof buf, "min = %u;", t->c_cc[VMIN]);
	tok(buf);
	snprintf(buf, sizeof buf, "time = %u;", t->c_cc[VTIME]);
	tok(buf);
	tok_end();

	g_cont = "";
}

static void show_brief(const struct termios *t)
{
	struct termios ref = *t;
	char buf[40], val[16];
	unsigned cs, rcs;
	int i;

	apply_sane(&ref);	/* sane reference, t's speed preserved */

	if (t->c_ispeed == t->c_ospeed)
		printf("speed %u baud;\n", t->c_ospeed);
	else
		printf("ispeed %u baud; ospeed %u baud;\n",
		    t->c_ispeed, t->c_ospeed);

	g_col = 0;
	g_cont = "";

	for (i = 0; i < NCC; i++) {
		cc_t cur = t->c_cc[cctab[i].idx];

		if (cur != ref.c_cc[cctab[i].idx]) {
			cc_str(cur, val, sizeof val);
			snprintf(buf, sizeof buf, "%s = %s;",
			    cctab[i].name, val);
			tok(buf);
		}
	}
	if (t->c_cc[VMIN] != ref.c_cc[VMIN]) {
		snprintf(buf, sizeof buf, "min = %u;", t->c_cc[VMIN]);
		tok(buf);
	}
	if (t->c_cc[VTIME] != ref.c_cc[VTIME]) {
		snprintf(buf, sizeof buf, "time = %u;", t->c_cc[VTIME]);
		tok(buf);
	}

	cs  = t->c_cflag & CSIZE;
	rcs = ref.c_cflag & CSIZE;
	if (cs != rcs)
		tok(cs == CS5 ? "cs5" : cs == CS6 ? "cs6"
		  : cs == CS7 ? "cs7" : "cs8");

	for (i = 0; i < NFLAGS; i++) {
		tcflag_t cur = gf_get(t,    flagtab[i].grp) & flagtab[i].mask;
		tcflag_t rc  = gf_get(&ref, flagtab[i].grp) & flagtab[i].mask;

		if (!!cur != !!rc) {
			if (cur) {
				tok(flagtab[i].name);
			} else {
				snprintf(buf, sizeof buf, "-%s",
				    flagtab[i].name);
				tok(buf);
			}
		}
	}
	tok_end();
}

/* ---- gfmt1 save token ---------------------------------------------- */

static void show_gfmt1(const struct termios *t)
{
	int i;

	printf("gfmt1:cflag=%x:iflag=%x:lflag=%x:oflag=%x",
	    (unsigned)t->c_cflag, (unsigned)t->c_iflag,
	    (unsigned)t->c_lflag, (unsigned)t->c_oflag);
	for (i = 0; i < NGFCC; i++)
		printf(":%s=%x", gfcc[i].name,
		    (unsigned)t->c_cc[gfcc[i].idx]);
	printf(":line=%x:ispeed=%u:ospeed=%u\n",
	    (unsigned)t->c_line, t->c_ispeed, t->c_ospeed);
}

static void gfmt1_kv(struct termios *t, const char *key, const char *val)
{
	unsigned long n;
	int i;

	if (!strcmp(key, "ispeed")) {
		t->c_ispeed = (speed_t)strtoul(val, NULL, 10);
		return;
	}
	if (!strcmp(key, "ospeed")) {
		t->c_ospeed = (speed_t)strtoul(val, NULL, 10);
		return;
	}
	n = strtoul(val, NULL, 16);
	if (!strcmp(key, "cflag")) { t->c_cflag = (tcflag_t)n; return; }
	if (!strcmp(key, "iflag")) { t->c_iflag = (tcflag_t)n; return; }
	if (!strcmp(key, "lflag")) { t->c_lflag = (tcflag_t)n; return; }
	if (!strcmp(key, "oflag")) { t->c_oflag = (tcflag_t)n; return; }
	if (!strcmp(key, "line"))  { t->c_line  = (cc_t)n;     return; }
	for (i = 0; i < NGFCC; i++)
		if (!strcmp(key, gfcc[i].name)) {
			t->c_cc[gfcc[i].idx] = (cc_t)n;
			return;
		}
	/* unknown key: ignore for forward compatibility */
}

static void load_gfmt1(const char *s, struct termios *t)
{
	const char *p = s + 6;		/* skip "gfmt1:" */

	while (*p) {
		char key[32], val[32];
		int ki = 0, vi = 0;

		while (*p && *p != '=' && *p != ':') {
			if (ki < (int)sizeof key - 1)
				key[ki++] = *p;
			p++;
		}
		key[ki] = '\0';
		if (*p == ':') {		/* token with no '=' */
			p++;
			if (ki == 0)
				continue;	/* empty token */
			fatal("invalid stty-readable argument");
		}
		if (*p != '=')
			fatal("invalid stty-readable argument");
		p++;
		while (*p && *p != ':') {
			if (vi < (int)sizeof val - 1)
				val[vi++] = *p;
			p++;
		}
		val[vi] = '\0';
		if (*p == ':')
			p++;
		gfmt1_kv(t, key, val);
	}
}

/* ---- operand value parsing ----------------------------------------- */

static int alldigits(const char *s)
{
	if (!*s)
		return 0;
	for (; *s; s++)
		if (!isdigit((unsigned char)*s))
			return 0;
	return 1;
}

static long getnum(const char *s, const char *what)
{
	char *end;
	long v;

	v = strtol(s, &end, 10);
	if (end == s || *end)
		fatal2("invalid integer argument", what);
	return v;
}

static int parse_cc(const char *s)
{
	char *end;

	if (s[0] == '\0' || !strcmp(s, "^-") || !strcmp(s, "undef"))
		return 0;
	if (s[0] == '^' && s[1] != '\0' && s[2] == '\0') {
		if (s[1] == '?')
			return 127;
		return toupper((unsigned char)s[1]) & 0x1f;
	}
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		return (int)strtol(s, &end, 16) & 0xff;
	if (s[0] == '0' && s[1] != '\0')
		return (int)strtol(s, &end, 8) & 0xff;
	if (isdigit((unsigned char)s[0]))
		return (int)strtol(s, &end, 10) & 0xff;
	if (s[1] == '\0')
		return (unsigned char)s[0];
	fatal2("invalid control-character value", s);
	return 0;
}

/* ---- operand application ------------------------------------------- */

static int g_action = TCSADRAIN;	/* tcsetattr action */

/*
 * Apply one operand.  Returns the number of argv entries consumed
 * (1, or 2 for an operand that takes a following value).
 */
static int set_operand(int idx, int argc, char **argv, struct termios *t,
    struct winsize *ws, int *ws_dirty)
{
	char *op = argv[idx];
	const char *name = op;
	int neg = 0, i;

	if (op[0] == '-' && op[1] != '\0') {
		neg  = 1;
		name = op + 1;
	}

	/* bare unsigned number => both line speeds */
	if (!neg && alldigits(name)) {
		t->c_ispeed = t->c_ospeed = (speed_t)strtoul(name, NULL, 10);
		return 1;
	}

	/* boolean modes + accepted-only synonyms */
	for (i = 0; i < NFLAGS; i++)
		if (!strcmp(name, flagtab[i].name)) {
			gf_set(t, flagtab[i].grp, flagtab[i].mask, !neg);
			return 1;
		}
	for (i = 0; i < NALIAS; i++)
		if (!strcmp(name, aliastab[i].name)) {
			gf_set(t, aliastab[i].grp, aliastab[i].mask, !neg);
			return 1;
		}

	/* character size */
	if (!neg && !strncmp(name, "cs", 2) && name[3] == '\0' &&
	    name[2] >= '5' && name[2] <= '8') {
		tcflag_t sz = name[2] == '5' ? CS5 : name[2] == '6' ? CS6
		            : name[2] == '7' ? CS7 : CS8;
		t->c_cflag = (t->c_cflag & ~CSIZE) | sz;
		return 1;
	}

	/* combination keywords */
	if (!strcmp(name, "sane")) {
		if (neg)
			fatal2("invalid argument", op);
		apply_sane(t);
		return 1;
	}
	if (!strcmp(name, "ek")) {
		if (neg)
			fatal2("invalid argument", op);
		t->c_cc[VERASE] = 127;
		t->c_cc[VKILL]  = 21;
		return 1;
	}
	if (!strcmp(name, "raw")) {
		apply_raw(t, !neg);
		return 1;
	}
	if (!strcmp(name, "cooked")) {
		apply_raw(t, neg);
		return 1;
	}
	if (!strcmp(name, "cbreak")) {
		gf_set(t, FL, ICANON, neg);
		return 1;
	}
	if (!strcmp(name, "nl")) {
		gf_set(t, FI, ICRNL, neg);
		gf_set(t, FO, ONLCR, neg);
		return 1;
	}
	if (!strcmp(name, "evenp") || !strcmp(name, "parity")) {
		apply_parity(t, !neg, 0);
		return 1;
	}
	if (!strcmp(name, "oddp")) {
		apply_parity(t, !neg, 1);
		return 1;
	}
	if (!strcmp(name, "crt")) {
		if (neg)
			fatal2("invalid argument", op);
		t->c_lflag |= ECHOE | ECHOCTL | ECHOKE;
		return 1;
	}
	if (!strcmp(name, "dec")) {
		if (neg)
			fatal2("invalid argument", op);
		t->c_lflag |= ECHOE | ECHOCTL | ECHOKE;
		t->c_iflag &= ~IXANY;
		t->c_cc[VINTR]  = 3;
		t->c_cc[VERASE] = 127;
		t->c_cc[VKILL]  = 21;
		return 1;
	}
	if (!strcmp(name, "decctlq")) {
		gf_set(t, FI, IXANY, neg);
		return 1;
	}
	if (!strcmp(name, "pass8")) {
		if (neg) {
			t->c_cflag = (t->c_cflag & ~CSIZE) | CS7 | PARENB;
			t->c_iflag |= ISTRIP;
		} else {
			t->c_cflag = (t->c_cflag & ~(CSIZE | PARENB)) | CS8;
			t->c_iflag &= ~ISTRIP;
		}
		return 1;
	}
	if (!strcmp(name, "litout")) {
		if (neg) {
			t->c_cflag = (t->c_cflag & ~CSIZE) | CS7 | PARENB;
			t->c_iflag |= ISTRIP;
			t->c_oflag |= OPOST;
		} else {
			t->c_cflag = (t->c_cflag & ~(CSIZE | PARENB)) | CS8;
			t->c_iflag &= ~ISTRIP;
			t->c_oflag &= ~OPOST;
		}
		return 1;
	}
	if (!strcmp(name, "drain")) {
		g_action = neg ? TCSANOW : TCSADRAIN;
		return 1;
	}

	/* control characters (take a following value) */
	if (!neg) {
		for (i = 0; i < NCC; i++)
			if (!strcmp(name, cctab[i].name)) {
				if (idx + 1 >= argc)
					fatal2("missing argument to", op);
				t->c_cc[cctab[i].idx] =
				    (cc_t)parse_cc(argv[idx + 1]);
				return 2;
			}
		for (i = 0; i < NCCALIAS; i++)
			if (!strcmp(name, ccaliastab[i].name)) {
				if (idx + 1 >= argc)
					fatal2("missing argument to", op);
				t->c_cc[ccaliastab[i].idx] =
				    (cc_t)parse_cc(argv[idx + 1]);
				return 2;
			}

		/* numeric / special operands */
		if (!strcmp(name, "min") || !strcmp(name, "time")) {
			int ci = name[0] == 'm' ? VMIN : VTIME;

			if (idx + 1 >= argc)
				fatal2("missing argument to", op);
			t->c_cc[ci] = (cc_t)getnum(argv[idx + 1], op);
			return 2;
		}
		if (!strcmp(name, "rows") || !strcmp(name, "cols") ||
		    !strcmp(name, "columns")) {
			if (idx + 1 >= argc)
				fatal2("missing argument to", op);
			if (name[0] == 'r')
				ws->ws_row =
				    (unsigned short)getnum(argv[idx + 1], op);
			else
				ws->ws_col =
				    (unsigned short)getnum(argv[idx + 1], op);
			*ws_dirty = 1;
			return 2;
		}
		if (!strcmp(name, "ispeed") || !strcmp(name, "ospeed")) {
			if (idx + 1 >= argc)
				fatal2("missing argument to", op);
			if (name[0] == 'i')
				t->c_ispeed =
				    (speed_t)getnum(argv[idx + 1], op);
			else
				t->c_ospeed =
				    (speed_t)getnum(argv[idx + 1], op);
			return 2;
		}
		if (!strcmp(name, "line")) {
			if (idx + 1 >= argc)
				fatal2("missing argument to", op);
			t->c_line = (cc_t)getnum(argv[idx + 1], op);
			return 2;
		}
	}

	fatal2("invalid argument", op);
	return 1;
}

/* ---- main ---------------------------------------------------------- */

int main(int argc, char **argv)
{
	const char *device = NULL;
	int do_all = 0, do_g = 0;
	int ai, nop, fd, ws_dirty = 0;
	char **ops;
	struct termios t;
	struct winsize ws;

	for (ai = 1; ai < argc; ai++) {
		char *a = argv[ai];

		if (!strcmp(a, "--")) {
			ai++;
			break;
		}
		if (!strcmp(a, "--help"))
			help();
		if (!strcmp(a, "-a") || !strcmp(a, "--all") ||
		    !strcmp(a, "-e")) {
			do_all = 1;
			continue;
		}
		if (!strcmp(a, "-g") || !strcmp(a, "--save")) {
			do_g = 1;
			continue;
		}
		if (!strcmp(a, "-F") || !strcmp(a, "-f") ||
		    !strcmp(a, "--file")) {
			if (++ai >= argc)
				fatal("device option requires an argument");
			device = argv[ai];
			continue;
		}
		if ((!strncmp(a, "-F", 2) || !strncmp(a, "-f", 2)) && a[2])
			device = a + 2;
		else if (!strncmp(a, "--file=", 7))
			device = a + 7;
		else
			break;			/* first operand */
	}

	nop = argc - ai;
	ops = argv + ai;

	if ((do_all || do_g) && nop > 0)
		fatal("modes may not be set when reporting settings");
	if (do_all && do_g)
		fatal("verbose and stty-readable output styles are "
		    "mutually exclusive");

	fd = STDIN_FILENO;
	if (device) {
		fd = open(device, O_RDONLY | O_NONBLOCK | O_NOCTTY);
		if (fd < 0)
			fatal2("cannot open", device);
	}

	if (tcgetattr(fd, &t) != 0)
		fatal2(device ? device : "standard input", strerror(errno));

	memset(&ws, 0, sizeof ws);
	ioctl(fd, TIOCGWINSZ, &ws);		/* best effort */

	if (nop == 0) {
		if (do_g)
			show_gfmt1(&t);
		else if (do_all)
			show_verbose(&t, &ws);
		else
			show_brief(&t);
		return 0;
	}

	if (nop == 1 && !strcmp(ops[0], "size")) {
		printf("%u %u\n", ws.ws_row, ws.ws_col);
		return 0;
	}
	if (nop == 1 && !strcmp(ops[0], "speed")) {
		printf("%u\n", t.c_ospeed);
		return 0;
	}

	for (ai = 0; ai < nop; ) {
		if (ai == 0 && !strncmp(ops[0], "gfmt1:", 6)) {
			load_gfmt1(ops[0], &t);
			ai++;
			continue;
		}
		ai += set_operand(ai, nop, ops, &t, &ws, &ws_dirty);
	}

	if (tcsetattr(fd, g_action, &t) != 0)
		fatal2("tcsetattr", strerror(errno));
	if (ws_dirty && ioctl(fd, TIOCSWINSZ, &ws) != 0)
		fatal2("TIOCSWINSZ", strerror(errno));

	return 0;
}
