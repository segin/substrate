#include "cp_copy.h"
#include "cp_opts.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t g_stop_requested = 0;

static void cp_signal_handler(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static const char *cp_progname(const char *argv0)
{
    const char *slash = strrchr(argv0, '/');
    return slash ? slash + 1 : argv0;
}

static void cp_install_signal_handlers(void)
{
#ifdef CP_HOST_BUILD
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cp_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);
#else
    signal(SIGINT, cp_signal_handler);
    signal(SIGTERM, cp_signal_handler);
#endif
}

int main(int argc, char **argv)
{
    struct cp_options opts;
    const char *err_msg = NULL;
    const char *progname = cp_progname(argv[0]);
    struct cp_context ctx;
    int rc;

    if (cp_parse_options(&opts, argc, argv, &err_msg) != 0) {
        fprintf(stderr, "%s: %s\n", progname, err_msg ? err_msg : "invalid arguments");
        fputs(cp_options_usage(progname), stderr);
        return 1;
    }

    if (opts.show_help) {
        fputs(cp_options_usage(progname), stdout);
        puts("Options:");
        puts("  -R, -r, --recursive          recurse into directories");
        puts("  -H                           follow command-line symlinks when recursing");
        puts("  -L, --dereference            follow all symlinks");
        puts("  -P, --no-dereference         copy symlink objects (default recursive mode)");
        puts("  -f, --force                  force overwrite and suppress warnings");
        puts("  -i, --interactive            prompt before overwrite");
        puts("  -n, --no-clobber             never overwrite existing files");
        puts("  -p[a|mode], --preserve=LIST  preserve metadata (mode/owner/time, or all)");
        puts("  -a, --archive                archive mode (-R and preserve all)");
        puts("  -l, --link                   make hard links instead of copying");
        puts("  -s, --symbolic-link          make symbolic links instead of copying");
        puts("  -b, --buffer-size=SIZE       set copy buffer size");
        puts("      --sparse=MODE            sparse strategy: auto|always|never");
        puts("      --atomic-replace         write temp file + rename (default)");
        puts("      --no-atomic-replace      disable atomic replacement");
        puts("      --progress               print per-file progress lines");
        puts("      --interactive-default=Y  non-tty prompt default yes|no");
        puts("      --help                   show this help");
        puts("      --version                show version");
        return 0;
    }

    if (opts.show_version) {
        printf("%s (Substrate cp) 1.0\n", progname);
        return 0;
    }

    cp_install_signal_handlers();

    if (cp_context_init(&ctx, &opts, progname, &g_stop_requested) != 0) {
        fprintf(stderr, "%s: failed to initialize copy context\n", progname);
        return 1;
    }

    rc = cp_execute(&ctx, argc, argv);
    cp_context_destroy(&ctx);

    return rc;
}
