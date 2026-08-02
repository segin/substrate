#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "echo.h"
#include "echo_escape.h"
#include "echo_opts.h"
#include "echo_write.h"

struct echo_output {
    int fd;
};

static void
echo_print_usage(FILE *stream, const char *progname)
{
    fprintf(stream,
        "Usage: %s [-n] [-e|-E] [ARG]...\n"
        "       %s [--help] [--version]\n",
        progname, progname);
}

static void
echo_print_help(const char *progname)
{
    echo_print_usage(stdout, progname);
    fputs(
        "\n"
        "Write arguments separated by a single space to standard output.\n"
        "\n"
        "Options:\n"
        "  -n          do not output the trailing newline\n"
        "  -e          enable interpretation of backslash escapes\n"
        "  -E          disable interpretation of backslash escapes (default)\n"
        "  --help      display this help and exit\n"
        "  --version   output version information and exit\n",
        stdout);
}

static void
echo_print_version(void)
{
    puts(ECHO_VERSION);
}

static void
echo_warn_write(const struct echo_options *options)
{
    /* A broken pipe (reader went away) is normal for a filter; exit quietly
     * without a diagnostic, like a SIGPIPE death (ECHO-03). */
    if (errno == EPIPE)
        return;
    fprintf(stderr, "%s: write failed: %s\n", options->progname, strerror(errno));
}

static int
echo_fd_emit(void *ctx, const unsigned char *data, size_t len)
{
    struct echo_output *output;

    output = (struct echo_output *)ctx;
    return echo_write_all(output->fd, data, len);
}

int
main(int argc, char *argv[])
{
    struct echo_options options;
    struct echo_output output;
    bool stop_output;
    int index;

    output.fd = STDOUT_FILENO;
    stop_output = false;

    echo_options_init(&options, argv[0]);
    if (echo_parse_options(&options, argc, argv) != 0) {
        echo_print_usage(stderr, options.progname);
        return 1;
    }
    if (options.show_help) {
        echo_print_help(options.progname);
        return 0;
    }
    if (options.show_version) {
        echo_print_version();
        return 0;
    }

    for (index = options.arg_index; index < argc; ++index) {
        if (index > options.arg_index) {
            if (echo_fd_emit(&output, (const unsigned char *)" ", 1) != 0) {
                echo_warn_write(&options);
                return 1;
            }
        }
        if (echo_emit_text(argv[index], options.enable_escapes, echo_fd_emit,
                &output, &stop_output) != 0) {
            echo_warn_write(&options);
            return 1;
        }
        if (stop_output) {
            break;
        }
    }

    if (!options.no_newline && !stop_output) {
        if (echo_fd_emit(&output, (const unsigned char *)"\n", 1) != 0) {
            echo_warn_write(&options);
            return 1;
        }
    }

    return 0;
}
