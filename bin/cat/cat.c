/*
 * cat - concatenate files and print on standard output
 *
 * POSIX.1-2017 compliant with common extensions
 * Options:
 *   -u          Write bytes unbuffered (POSIX)
 *   -n          Number all output lines
 *   -b          Number non-blank lines only
 *   -s          Squeeze repeated blank lines
 *   -E          Display $ at end of each line
 *   -T          Display TAB as ^I
 *   -v          Display non-printing characters
 *   -A          Equivalent to -vET
 *   -e          Equivalent to -vE
 *   -t          Equivalent to -vT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

/* Buffer size for I/O operations */
#define BUFSIZE 65536

/* Option flags */
static int opt_unbuffered = 0;    /* -u */
static int opt_number = 0;        /* -n */
static int opt_number_nonblank = 0; /* -b */
static int opt_squeeze = 0;       /* -s */
static int opt_show_ends = 0;     /* -E */
static int opt_show_tabs = 0;     /* -T */
static int opt_show_nonprint = 0; /* -v */

/* State for line numbering and blank squeezing */
static unsigned long line_number = 1;
static int at_line_start = 1;
static int prev_blank = 0;

/* Exit status */
static int exit_status = 0;

/* Program name for error messages */
static const char *progname = "cat";

/*
 * Write all bytes, handling partial writes and EINTR
 */
static ssize_t write_all(int fd, const char *buf, size_t count)
{
    size_t written = 0;
    
    while (written < count) {
        ssize_t n = write(fd, buf + written, count - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        written += n;
    }
    return written;
}

/*
 * Write a single character
 */
static int write_char(char c)
{
    return write_all(STDOUT_FILENO, &c, 1) == 1 ? 0 : -1;
}

/*
 * Write a string
 */
static int write_str(const char *s)
{
    size_t len = strlen(s);
    return write_all(STDOUT_FILENO, s, len) == (ssize_t)len ? 0 : -1;
}

/*
 * Output a line number
 */
static int output_line_number(void)
{
    char numbuf[16];
    int len = snprintf(numbuf, sizeof(numbuf), "%6lu\t", line_number);
    line_number++;
    return write_all(STDOUT_FILENO, numbuf, len) == len ? 0 : -1;
}

/*
 * Output a non-printing character in ^X or M-X notation
 */
static int output_nonprint(unsigned char c)
{
    if (c < 32) {
        /* Control characters: ^@ through ^_ */
        if (write_char('^') < 0) return -1;
        if (write_char(c + '@') < 0) return -1;
    } else if (c == 127) {
        /* DEL: ^? */
        if (write_char('^') < 0) return -1;
        if (write_char('?') < 0) return -1;
    } else if (c >= 128) {
        /* High-bit characters: M-X notation */
        if (write_str("M-") < 0) return -1;
        c -= 128;
        if (c < 32) {
            if (write_char('^') < 0) return -1;
            if (write_char(c + '@') < 0) return -1;
        } else if (c == 127) {
            if (write_char('^') < 0) return -1;
            if (write_char('?') < 0) return -1;
        } else {
            if (write_char(c) < 0) return -1;
        }
    } else {
        /* Printable: output directly */
        if (write_char(c) < 0) return -1;
    }
    return 0;
}

/*
 * Process a single byte with all active options
 */
static int process_byte(unsigned char c)
{
    int is_newline = (c == '\n');
    int is_blank_line = is_newline && at_line_start;
    
    /* Squeeze blank lines */
    if (opt_squeeze && is_blank_line) {
        if (prev_blank) {
            return 0;  /* Skip this blank line */
        }
        prev_blank = 1;
    } else if (!is_blank_line) {
        prev_blank = 0;
    }
    
    /* Line numbering at start of line */
    if (at_line_start && !is_blank_line) {
        if (opt_number || opt_number_nonblank) {
            if (output_line_number() < 0) return -1;
        }
    } else if (at_line_start && is_blank_line && opt_number && !opt_number_nonblank) {
        if (output_line_number() < 0) return -1;
    }
    
    /* Show end-of-line marker before newline */
    if (is_newline && opt_show_ends) {
        if (write_char('$') < 0) return -1;
    }
    
    /* Update line start state */
    at_line_start = is_newline;
    
    /* Handle the character */
    if (c == '\t' && opt_show_tabs) {
        if (write_char('^') < 0) return -1;
        if (write_char('I') < 0) return -1;
    } else if (c == '\n' || c == '\t') {
        /* Newline and tab (when not showing) pass through */
        if (write_char(c) < 0) return -1;
    } else if (opt_show_nonprint && (c < 32 || c >= 127)) {
        if (output_nonprint(c) < 0) return -1;
    } else {
        if (write_char(c) < 0) return -1;
    }
    
    return 0;
}

/*
 * Cat a file with options applied
 */
static int cat_file_with_options(int fd, const char *name)
{
    unsigned char buf[BUFSIZE];
    ssize_t n;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (process_byte(buf[i]) < 0) {
                if (errno == EPIPE) {
                    /* Broken pipe - exit silently */
                    exit(exit_status);
                }
                fprintf(stderr, "%s: write error: %s\n", progname, strerror(errno));
                return -1;
            }
        }
    }
    
    if (n < 0) {
        fprintf(stderr, "%s: %s: %s\n", progname, name, strerror(errno));
        return -1;
    }
    
    return 0;
}

/*
 * Simple cat without options (fast path)
 */
static int cat_file_simple(int fd, const char *name)
{
    char buf[BUFSIZE];
    ssize_t n;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write_all(STDOUT_FILENO, buf, n) < 0) {
            if (errno == EPIPE) {
                exit(exit_status);
            }
            fprintf(stderr, "%s: write error: %s\n", progname, strerror(errno));
            return -1;
        }
    }
    
    if (n < 0) {
        fprintf(stderr, "%s: %s: %s\n", progname, name, strerror(errno));
        return -1;
    }
    
    return 0;
}

/*
 * Cat a file (chooses fast or slow path based on options)
 */
static int cat_file(int fd, const char *name)
{
    if (opt_number || opt_number_nonblank || opt_squeeze || 
        opt_show_ends || opt_show_tabs || opt_show_nonprint) {
        return cat_file_with_options(fd, name);
    } else {
        return cat_file_simple(fd, name);
    }
}

/*
 * Open and cat a file by name
 */
static void cat_filename(const char *name)
{
    int fd;
    struct stat st;
    
    if (strcmp(name, "-") == 0) {
        if (cat_file(STDIN_FILENO, "standard input") < 0) {
            exit_status = 1;
        }
        return;
    }
    
    fd = open(name, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "%s: %s: %s\n", progname, name, strerror(errno));
        exit_status = 1;
        return;
    }
    
    /* Check if it's a directory */
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s: %s: Is a directory\n", progname, name);
        close(fd);
        exit_status = 1;
        return;
    }
    
    if (cat_file(fd, name) < 0) {
        exit_status = 1;
    }
    
    close(fd);
}

/*
 * Print usage information
 */
static void usage(void)
{
    fprintf(stderr, 
        "Usage: %s [OPTION]... [FILE]...\n"
        "Concatenate FILE(s) to standard output.\n\n"
        "With no FILE, or when FILE is -, read standard input.\n\n"
        "  -A, --show-all       equivalent to -vET\n"
        "  -b, --number-nonblank  number non-blank output lines\n"
        "  -e                   equivalent to -vE\n"
        "  -E, --show-ends      display $ at end of each line\n"
        "  -n, --number         number all output lines\n"
        "  -s, --squeeze-blank  suppress repeated empty output lines\n"
        "  -t                   equivalent to -vT\n"
        "  -T, --show-tabs      display TAB characters as ^I\n"
        "  -u                   (ignored)\n"
        "  -v, --show-nonprinting  use ^ and M- notation, except for LFD and TAB\n"
        "      --help           display this help and exit\n"
        "      --version        output version information and exit\n",
        progname);
}

/*
 * Print version information
 */
static void version(void)
{
    fprintf(stderr, "%s (Substrate coreutils) 1.0\n", progname);
}

/*
 * Parse command line options
 */
static int parse_options(int argc, char *argv[], int *optind_out)
{
    int i;
    
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') {
            /* Not an option or just "-" */
            break;
        }
        
        if (argv[i][1] == '-') {
            /* Long options */
            if (strcmp(argv[i], "--") == 0) {
                i++;
                break;
            } else if (strcmp(argv[i], "--help") == 0) {
                usage();
                exit(0);
            } else if (strcmp(argv[i], "--version") == 0) {
                version();
                exit(0);
            } else if (strcmp(argv[i], "--show-all") == 0) {
                opt_show_nonprint = 1;
                opt_show_ends = 1;
                opt_show_tabs = 1;
            } else if (strcmp(argv[i], "--number-nonblank") == 0) {
                opt_number_nonblank = 1;
            } else if (strcmp(argv[i], "--show-ends") == 0) {
                opt_show_ends = 1;
            } else if (strcmp(argv[i], "--number") == 0) {
                opt_number = 1;
            } else if (strcmp(argv[i], "--squeeze-blank") == 0) {
                opt_squeeze = 1;
            } else if (strcmp(argv[i], "--show-tabs") == 0) {
                opt_show_tabs = 1;
            } else if (strcmp(argv[i], "--show-nonprinting") == 0) {
                opt_show_nonprint = 1;
            } else {
                fprintf(stderr, "%s: unrecognized option '%s'\n", progname, argv[i]);
                fprintf(stderr, "Try '%s --help' for more information.\n", progname);
                return -1;
            }
        } else {
            /* Short options */
            for (const char *p = argv[i] + 1; *p; p++) {
                switch (*p) {
                case 'A':
                    opt_show_nonprint = 1;
                    opt_show_ends = 1;
                    opt_show_tabs = 1;
                    break;
                case 'b':
                    opt_number_nonblank = 1;
                    break;
                case 'e':
                    opt_show_nonprint = 1;
                    opt_show_ends = 1;
                    break;
                case 'E':
                    opt_show_ends = 1;
                    break;
                case 'n':
                    opt_number = 1;
                    break;
                case 's':
                    opt_squeeze = 1;
                    break;
                case 't':
                    opt_show_nonprint = 1;
                    opt_show_tabs = 1;
                    break;
                case 'T':
                    opt_show_tabs = 1;
                    break;
                case 'u':
                    opt_unbuffered = 1;
                    /* -u is ignored, we're already unbuffered for writes */
                    break;
                case 'v':
                    opt_show_nonprint = 1;
                    break;
                default:
                    fprintf(stderr, "%s: invalid option -- '%c'\n", progname, *p);
                    fprintf(stderr, "Try '%s --help' for more information.\n", progname);
                    return -1;
                }
            }
        }
    }
    
    /* -b overrides -n */
    if (opt_number_nonblank)
        opt_number = 0;
    
    *optind_out = i;
    return 0;
}

int main(int argc, char *argv[])
{
    int optind;
    
    progname = argv[0];
    
    if (parse_options(argc, argv, &optind) < 0) {
        return 1;
    }
    
    if (optind >= argc) {
        /* No files specified, read stdin */
        cat_filename("-");
    } else {
        for (int i = optind; i < argc; i++) {
            cat_filename(argv[i]);
        }
    }
    
    return exit_status;
}
