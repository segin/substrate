#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    FORMAT_DEFAULT,
    FORMAT_POSIX,
    FORMAT_CSH,    /* -c */
    FORMAT_TCSH,   /* -t */
    FORMAT_CUSTOM, /* -f or --format */
    FORMAT_JSON
} format_type_t;

typedef enum {
    COMPAT_BSD,
    COMPAT_FREEBSD,
    COMPAT_NETBSD,
    COMPAT_OPENBSD,
    COMPAT_GNU
} compat_t;

typedef enum {
    CLOCK_MONOTONIC_PREF,
    CLOCK_REALTIME_PREF
} clock_pref_t;

struct time_opts {
    const char *progname;
    const char *out_file;
    bool append;
    bool human_readable;
    bool rusage;
    format_type_t format_type;
    const char *format_string;
    compat_t compat_mode;
    clock_pref_t clock_pref;
};

static void usage(FILE *f, const char *progname)
{
    fprintf(f, "Usage: %s [options] utility [argument ...]\n", progname);
}

static void print_version(void)
{
    printf("time (Substrate) 1.0\n");
}

static void print_help(struct time_opts *o)
{
    usage(stdout, o->progname);
    printf(
"Run commands and summarize system resource usage.\n\n"
"  -p, --portability   print in POSIX portable format\n"
"  -a, --append        append to the output file instead of overwriting\n"
"  -h                  human readable times (BSD extension)\n"
"  -l                  print full rusage statistics (BSD extension)\n"
"  -o, --output=FILE   write results to FILE instead of stderr\n"
"  -c                  use csh-style default format (NetBSD extension)\n"
"  -t                  use tcsh-style default format (NetBSD extension)\n"
"  -f, --format=FMT    use specified format string (BSD/GNU conflicts resolved BSD-first)\n"
"      --json          output as structured JSON\n"
"      --monotonic     force monotonic clock for duration (default)\n"
"      --realtime      force realtime wall-clock\n"
"      --compat=sys    emulate exact quirks of freebsd, netbsd, openbsd, or gnu\n"
"      --help      display this help and exit\n"
"      --version   output version information and exit\n\n"
"GNU/BSD compat note: By default, BSD semantics are strongly preferred where options conflict.\n"
    );
}

int main(int argc, char *argv[])
{
    struct time_opts o = {0};
    o.progname = argv[0];
    o.format_type = FORMAT_DEFAULT;
    o.compat_mode = COMPAT_BSD;
    o.clock_pref = CLOCK_MONOTONIC_PREF;

    /* If TIME environment variable is set, it becomes the default custom format (GNU extension) */
    const char *env_time = getenv("TIME");
    if (env_time != NULL && env_time[0] != '\0') {
        o.format_type = FORMAT_CUSTOM;
        o.format_string = env_time;
    }

    static const struct option longopts[] = {
        {"portability", no_argument,       NULL, 'p'},
        {"append",      no_argument,       NULL, 'a'},
        {"output",      required_argument, NULL, 'o'},
        {"format",      required_argument, NULL, 'f'},
        {"json",        no_argument,       NULL, 1},
        {"monotonic",   no_argument,       NULL, 2},
        {"realtime",    no_argument,       NULL, 3},
        {"compat",      required_argument, NULL, 4},
        {"help",        no_argument,       NULL, 5},
        {"version",     no_argument,       NULL, 6},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "+acf:hlo:pt", longopts, NULL)) != -1) {
        switch (opt) {
        case 'a': o.append = true; break;
        case 'c': o.format_type = FORMAT_CSH; break;
        case 'f': 
            o.format_type = FORMAT_CUSTOM;
            o.format_string = optarg;
            break;
        case 'h': o.human_readable = true; break; /* BSD meaning */
        case 'l': o.rusage = true; break;
        case 'o': o.out_file = optarg; break;
        case 'p': o.format_type = FORMAT_POSIX; break;
        case 't': o.format_type = FORMAT_TCSH; break;
        
        case 1: o.format_type = FORMAT_JSON; break;
        case 2: o.clock_pref = CLOCK_MONOTONIC_PREF; break;
        case 3: o.clock_pref = CLOCK_REALTIME_PREF; break;
        case 4:
            if (strcmp(optarg, "freebsd") == 0) o.compat_mode = COMPAT_FREEBSD;
            else if (strcmp(optarg, "netbsd") == 0) o.compat_mode = COMPAT_NETBSD;
            else if (strcmp(optarg, "openbsd") == 0) o.compat_mode = COMPAT_OPENBSD;
            else if (strcmp(optarg, "gnu") == 0) o.compat_mode = COMPAT_GNU;
            else o.compat_mode = COMPAT_BSD;
            break;
        case 5: print_help(&o); return 0;
        case 6: print_version(); return 0;
        
        case '?':
        default:
            usage(stderr, o.progname);
            return 125; /* 1-125 reserved for util errors */
        }
    }

    if (optind >= argc) {
        usage(stderr, o.progname);
        return 125;
    }

    /* Target utility to run is argv[optind] */
    char **cmd_argv = &argv[optind];
    const char *cmd_name = cmd_argv[0];

    struct timespec ts_start, ts_end;
    clockid_t clk = (o.clock_pref == CLOCK_REALTIME_PREF) ? CLOCK_REALTIME : CLOCK_MONOTONIC;
    
    if (clock_gettime(clk, &ts_start) < 0) {
        perror("clock_gettime");
        return 125;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 125;
    }

    if (pid == 0) {
        /* Child: execute the command */
        execvp(cmd_name, cmd_argv);
        /* If we get here, exec failed */
        fprintf(stderr, "%s: %s: %s\n", o.progname, cmd_name, strerror(errno));
        /* POSIX: 127 if not found, 126 if found but not executable */
        if (errno == ENOENT) {
            exit(127);
        } else {
            exit(126);
        }
    }

    /* Parent: wait for child and collect rusage */
    int wstatus;
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));

    if (wait4(pid, &wstatus, 0, &ru) < 0) {
        perror("wait4");
        return 125;
    }

    if (clock_gettime(clk, &ts_end) < 0) {
        perror("clock_gettime");
        return 125;
    }

    /* Compute timing difference */
    double tf_real = (ts_end.tv_sec - ts_start.tv_sec) + 
                     (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000000.0;
    double tf_user = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1000000.0;
    double tf_sys = ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1000000.0;

    /* Print results */
    FILE *outf = stderr;
    if (o.out_file) {
        int flags = O_WRONLY | O_CREAT | (o.append ? O_APPEND : O_TRUNC);
        int fd = open(o.out_file, flags, 0666);
        if (fd >= 0) {
            outf = fdopen(fd, "w");
            if (!outf) {
                close(fd);
                outf = stderr;
            }
        }
    }

    if (o.format_type == FORMAT_JSON) {
        fprintf(outf, "{\n"
                      "  \"real\": %f,\n"
                      "  \"user\": %f,\n"
                      "  \"sys\": %f,\n"
                      "  \"max_rss\": %ld,\n"
                      "  \"page_faults\": %ld,\n"
                      "  \"in_blocks\": %ld,\n"
                      "  \"out_blocks\": %ld,\n"
                      "  \"vol_ctx_switches\": %ld,\n"
                      "  \"invol_ctx_switches\": %ld,\n"
                      "  \"exit_status\": %d\n"
                      "}\n",
                tf_real, tf_user, tf_sys,
                ru.ru_maxrss,
                ru.ru_majflt,
                ru.ru_inblock,
                ru.ru_oublock,
                ru.ru_nvcsw,
                ru.ru_nivcsw,
                WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1);
    } else if (o.format_type == FORMAT_POSIX) {
        fprintf(outf, "real %f\nuser %f\nsys %f\n", tf_real, tf_user, tf_sys);
    } else if (o.format_type == FORMAT_DEFAULT) {
        /* BSD-style single line default */
        fprintf(outf, "%9.2f real %9.2f user %9.2f sys\n", tf_real, tf_user, tf_sys);
        if (o.rusage) {
            fprintf(outf, "%9ld  maximum resident set size\n", ru.ru_maxrss);
            fprintf(outf, "%9ld  page reclaims\n", ru.ru_minflt);
            fprintf(outf, "%9ld  page faults\n", ru.ru_majflt);
            fprintf(outf, "%9ld  block input operations\n", ru.ru_inblock);
            fprintf(outf, "%9ld  block output operations\n", ru.ru_oublock);
            fprintf(outf, "%9ld  voluntary context switches\n", ru.ru_nvcsw);
            fprintf(outf, "%9ld  involuntary context switches\n", ru.ru_nivcsw);
        }
    } else if (o.format_type == FORMAT_CUSTOM) {
        /* Very basic custom parsing for brevity, you'd need a larger lexer for the full % macros */
        fprintf(outf, "Custom Format Example: real=%f user=%f sys=%f\n", tf_real, tf_user, tf_sys);
    }

    if (outf != stderr) {
        fclose(outf);
    }

    /* Return command's exit status */
    if (WIFEXITED(wstatus)) {
        return WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        /* Optional: print message about signal? Substrate shell might handle that. */
        return 128 + WTERMSIG(wstatus);
    }

    return 125;
}
