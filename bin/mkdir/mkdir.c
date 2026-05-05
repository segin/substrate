#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "modeparse.h"

#include <sys/stat.h>
#include <sys/types.h>

#include "mkdir.h"
#include "mkdir_opts.h"
#include "mkdir_parents.h"

static void
print_usage(FILE *stream, const char *progname)
{
    fprintf(stream,
        "Usage: %s [OPTION]... DIRECTORY...\n"
        "Create the DIRECTORY(ies), if they do not already exist.\n",
        progname);
}

static void
print_help(const char *progname)
{
    print_usage(stdout, progname);
    fputs(
        "\n"
        "Options:\n"
        "  -m MODE, --mode=MODE     set file mode (octal or symbolic)\n"
        "  -p, --parents            no error if existing, make parent directories as needed\n"
        "  -v, --verbose            print a message for each created directory\n"
        "  -Z, --context[=CTX]      request SELinux context handling (stub)\n"
        "      --help               display this help and exit\n"
        "      --version            output version information and exit\n",
        stdout);
}

static void
print_version(void)
{
    puts(MKDIR_VERSION);
}

static int
retry_open_dir(const char *path)
{
    int fd;

    do {
        fd = open(path, O_RDONLY | O_DIRECTORY);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

static int
retry_mkdir(const char *path, mode_t mode)
{
    int rc;

    do {
        rc = mkdir(path, mode);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static int
retry_fchmod(int fd, mode_t mode)
{
    int rc;

    do {
        rc = fchmod(fd, mode);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static void
warn_errno_path(const char *progname, const char *path, int errnum)
{
    fprintf(stderr, "%s: cannot create directory '%s': %s\n", progname,
        path, strerror(errnum));
}

static int
create_single_directory(const struct mkdir_options *opts, const char *path,
    mode_t create_mode, bool apply_final_mode, mode_t final_mode)
{
    int fd;

    if (retry_mkdir(path, create_mode) != 0) {
        return -1;
    }

    if (apply_final_mode) {
        fd = retry_open_dir(path);
        if (fd < 0) {
            return -1;
        }
        if (retry_fchmod(fd, final_mode) != 0) {
            int saved_errno = errno;

            (void)close(fd);
            errno = saved_errno;
            return -1;
        }
        (void)close(fd);
    }

    if (opts->verbose) {
        printf("%s: created directory '%s'\n", opts->progname, path);
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    struct mkdir_options opts;
    const char *err_msg = NULL;
    struct mode_change *compiled_mode = NULL;
    mode_t final_mode = 0777;
    int rval = 0;
    int index;

    mkdir_options_init(&opts, argv[0]);
    if (mkdir_parse_options(&opts, argc, argv, &err_msg) != 0) {
        print_usage(stderr, opts.progname);
        if (err_msg != NULL) {
            fprintf(stderr, "%s: %s\n", opts.progname, err_msg);
        }
        return 1;
    }

    if (opts.show_help) {
        print_help(opts.progname);
        return 0;
    }
    if (opts.show_version) {
        print_version();
        return 0;
    }

    if (opts.have_mode) {
        char mode_err[128];

        compiled_mode = modeparse_compile(opts.mode_string, mode_err,
            sizeof(mode_err));
        if (compiled_mode == NULL) {
            fprintf(stderr, "%s: invalid mode '%s': %s\n", opts.progname,
                opts.mode_string, mode_err);
            return 1;
        }
        final_mode = modeparse_apply(compiled_mode, S_IFDIR | 0777) & 07777;
    }

    if (opts.selinux_context_requested) {
        fprintf(stderr,
            "%s: warning: SELinux contexts are not supported; ignoring request\n",
            opts.progname);
    }

    for (index = opts.operand_start; index < argc; ++index) {
        const char *path = argv[index];

        if (path[0] == '\0') {
            warn_errno_path(opts.progname, path, ENOENT);
            rval = 1;
            continue;
        }

        if (opts.parents) {
            char *error_path = NULL;
            int error_errno = 0;

            if (mkdir_create_parents(&opts, path, 0777, opts.have_mode,
                    final_mode, &error_path, &error_errno) != 0) {
                warn_errno_path(opts.progname,
                    error_path != NULL ? error_path : path,
                    error_errno != 0 ? error_errno : errno);
                free(error_path);
                rval = 1;
                continue;
            }
            free(error_path);
            continue;
        }

        if (create_single_directory(&opts, path, 0777, opts.have_mode,
                final_mode) != 0) {
            warn_errno_path(opts.progname, path, errno);
            rval = 1;
        }
    }

    modeparse_free(compiled_mode);
    return rval;
}

