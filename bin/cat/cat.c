#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cat_cooked.h"

#define CAT_STACK_BUFSIZE ((size_t)BUFSIZ)
#define CAT_COOKED_SINK_BUFSIZE 4096u

struct cat_options {
    const char *progname;
    bool buffer_size_set;
    size_t buffer_size;

    bool number_nonblank;
    bool number_all;
    bool show_ends;
    bool fast_open;
    bool lock_stdout;
    bool squeeze_blank;
    bool show_tabs;
    bool unbuffered_stdout;
    bool show_nonprint;
};

struct cat_sink {
    int fd;
    unsigned char buf[CAT_COOKED_SINK_BUFSIZE];
    size_t used;
};

#ifdef CAT_TEST_HOOKS
struct cat_test_hooks {
    long read_eintr_every;
    size_t read_max;
    long write_eintr_every;
    size_t write_max;
    bool malloc_fail_once;
    long lock_eintr_count;
    int lock_fail_errno;

    unsigned long read_calls;
    unsigned long write_calls;
    bool malloc_failed;
};

static struct cat_test_hooks g_cat_test_hooks;
static bool g_cat_test_hooks_init;

static long cat_env_to_long(const char *name, long default_value)
{
    char *end = NULL;
    const char *value = getenv(name);

    if (value == NULL || value[0] == '\0') {
        return default_value;
    }

    {
        long parsed = strtol(value, &end, 10);
        if (end == value || *end != '\0' || parsed < 0) {
            return default_value;
        }
        return parsed;
    }
}

static void cat_test_hooks_load(void)
{
    if (g_cat_test_hooks_init) {
        return;
    }

    g_cat_test_hooks.read_eintr_every = cat_env_to_long("CAT_TEST_READ_EINTR_EVERY", 0);
    g_cat_test_hooks.read_max = (size_t)cat_env_to_long("CAT_TEST_READ_MAX", 0);
    g_cat_test_hooks.write_eintr_every = cat_env_to_long("CAT_TEST_WRITE_EINTR_EVERY", 0);
    g_cat_test_hooks.write_max = (size_t)cat_env_to_long("CAT_TEST_WRITE_MAX", 0);
    g_cat_test_hooks.malloc_fail_once = cat_env_to_long("CAT_TEST_MALLOC_FAIL", 0) != 0;
    g_cat_test_hooks.lock_eintr_count = cat_env_to_long("CAT_TEST_LOCK_EINTR_COUNT", 0);
    g_cat_test_hooks.lock_fail_errno = (int)cat_env_to_long("CAT_TEST_LOCK_FAIL_ERRNO", 0);
    g_cat_test_hooks_init = true;
}
#endif

static bool cat_is_stdin_name(const char *name)
{
    return strcmp(name, "-") == 0 || strcmp(name, "--") == 0;
}

static void cat_usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [-B bsize] [-beflnstuv] [-] [file ...]\n", progname);
}

static void cat_warn_file(const struct cat_options *options, const char *name)
{
    fprintf(stderr, "%s: %s: %s\n", options->progname, name, strerror(errno));
}

static void cat_warn_stdout(const struct cat_options *options)
{
    fprintf(stderr, "%s: stdout: %s\n", options->progname, strerror(errno));
}

static void cat_warnx(const struct cat_options *options, const char *message)
{
    fprintf(stderr, "%s: %s\n", options->progname, message);
}

static ssize_t cat_sys_read(int fd, void *buf, size_t count)
{
#ifdef CAT_TEST_HOOKS
    cat_test_hooks_load();
    g_cat_test_hooks.read_calls++;
    if (g_cat_test_hooks.read_eintr_every > 0 &&
        (g_cat_test_hooks.read_calls % (unsigned long)g_cat_test_hooks.read_eintr_every) == 0) {
        errno = EINTR;
        return -1;
    }
    if (g_cat_test_hooks.read_max > 0 && count > g_cat_test_hooks.read_max) {
        count = g_cat_test_hooks.read_max;
    }
#endif

    return read(fd, buf, count);
}

static ssize_t cat_sys_write(int fd, const void *buf, size_t count)
{
#ifdef CAT_TEST_HOOKS
    cat_test_hooks_load();
    g_cat_test_hooks.write_calls++;

    if (g_cat_test_hooks.write_eintr_every > 0 &&
        (g_cat_test_hooks.write_calls % (unsigned long)g_cat_test_hooks.write_eintr_every) == 0) {
        errno = EINTR;
        return -1;
    }

    if (g_cat_test_hooks.write_max > 0 && count > g_cat_test_hooks.write_max) {
        count = g_cat_test_hooks.write_max;
    }
#endif

    return write(fd, buf, count);
}

static void *cat_sys_malloc(size_t size)
{
#ifdef CAT_TEST_HOOKS
    cat_test_hooks_load();
    if (g_cat_test_hooks.malloc_fail_once && !g_cat_test_hooks.malloc_failed) {
        g_cat_test_hooks.malloc_failed = true;
        errno = ENOMEM;
        return NULL;
    }
#endif

    return malloc(size);
}

static int cat_sys_fcntl_getfl(int fd)
{
    return fcntl(fd, F_GETFL);
}

static int cat_sys_fcntl_setfl(int fd, int flags)
{
    return fcntl(fd, F_SETFL, flags);
}

static int cat_sys_fcntl_lock(int fd, int cmd, struct flock *lock)
{
#ifdef CAT_TEST_HOOKS
    cat_test_hooks_load();
    if (cmd == F_SETLKW && g_cat_test_hooks.lock_eintr_count > 0) {
        g_cat_test_hooks.lock_eintr_count--;
        errno = EINTR;
        return -1;
    }
    if (cmd == F_SETLKW && g_cat_test_hooks.lock_fail_errno != 0) {
        errno = g_cat_test_hooks.lock_fail_errno;
        return -1;
    }
#endif

    return fcntl(fd, cmd, lock);
}

static int cat_sys_open_ro(const char *path, int flags)
{
    return open(path, O_RDONLY | flags);
}

static int cat_parse_buffer_size(const char *text, size_t *value_out)
{
    size_t value = 0;
    int base = 10;
    size_t i = 0;

    if (text == NULL || text[0] == '\0') {
        return -1;
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        i = 2;
        if (text[i] == '\0') {
            return -1;
        }
    }

    for (; text[i] != '\0'; i++) {
        unsigned int digit;
        unsigned char ch = (unsigned char)text[i];

        if (ch >= '0' && ch <= '9') {
            digit = (unsigned int)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = 10u + (unsigned int)(ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            digit = 10u + (unsigned int)(ch - 'A');
        } else {
            return -1;
        }

        if ((int)digit >= base) {
            return -1;
        }

        if (value > (SIZE_MAX - (size_t)digit) / (size_t)base) {
            errno = ERANGE;
            return -1;
        }

        value = (value * (size_t)base) + (size_t)digit;
    }

    if (value == 0) {
        return -1;
    }

    *value_out = value;
    return 0;
}

static int cat_parse_options(int argc, char **argv, struct cat_options *options)
{
    int ch;

    opterr = 0;
    while ((ch = getopt(argc, argv, "B:beflnstuv")) != -1) {
        switch (ch) {
        case 'B':
            if (cat_parse_buffer_size(optarg, &options->buffer_size) < 0) {
                cat_warnx(options, "invalid buffer size for -B");
                return -1;
            }
            options->buffer_size_set = true;
            break;
        case 'b':
            options->number_nonblank = true;
            options->number_all = true;
            break;
        case 'e':
            options->show_nonprint = true;
            options->show_ends = true;
            break;
        case 'f':
            options->fast_open = true;
            break;
        case 'l':
            options->lock_stdout = true;
            break;
        case 'n':
            options->number_all = true;
            break;
        case 's':
            options->squeeze_blank = true;
            break;
        case 't':
            options->show_nonprint = true;
            options->show_tabs = true;
            break;
        case 'u':
            options->unbuffered_stdout = true;
            break;
        case 'v':
            options->show_nonprint = true;
            break;
        default:
            cat_usage(options->progname);
            return -1;
        }
    }

    return 0;
}

static bool cat_use_cooked_mode(const struct cat_options *options)
{
    return options->number_all ||
           options->number_nonblank ||
           options->show_ends ||
           options->squeeze_blank ||
           options->show_tabs ||
           options->show_nonprint;
}

static int cat_write_all(const unsigned char *data, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = cat_sys_write(STDOUT_FILENO, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            errno = EIO;
            return -1;
        }
        off += (size_t)n;
    }

    return 0;
}

static int cat_sink_flush(struct cat_sink *sink)
{
    if (sink->used == 0) {
        return 0;
    }

    if (cat_write_all(sink->buf, sink->used) < 0) {
        return -1;
    }

    sink->used = 0;
    return 0;
}

static int cat_sink_emit(void *ctx, const unsigned char *data, size_t len)
{
    struct cat_sink *sink = (struct cat_sink *)ctx;
    size_t off = 0;

    while (off < len) {
        size_t room = sizeof(sink->buf) - sink->used;
        size_t chunk;

        if (room == 0) {
            if (cat_sink_flush(sink) < 0) {
                return -1;
            }
            room = sizeof(sink->buf);
        }

        chunk = len - off;
        if (chunk > room) {
            chunk = room;
        }

        memcpy(sink->buf + sink->used, data + off, chunk);
        sink->used += chunk;
        off += chunk;
    }

    return 0;
}

static int cat_lock_stdout_fd(const struct cat_options *options)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    for (;;) {
        if (cat_sys_fcntl_lock(STDOUT_FILENO, F_SETLKW, &lock) == 0) {
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }

        fprintf(stderr, "%s: unable to lock stdout: %s\n", options->progname, strerror(errno));
        return -1;
    }
}

static int cat_unlock_stdout_fd(const struct cat_options *options)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    for (;;) {
        if (cat_sys_fcntl_lock(STDOUT_FILENO, F_SETLK, &lock) == 0) {
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }

        fprintf(stderr, "%s: unable to unlock stdout: %s\n", options->progname, strerror(errno));
        return -1;
    }
}

struct cat_raw_buffer {
    unsigned char stack_buf[CAT_STACK_BUFSIZE];
    unsigned char *buf;
    size_t len;
    bool heap_allocated;
};

static int cat_raw_buffer_init(struct cat_raw_buffer *raw, const struct cat_options *options)
{
    size_t selected = CAT_STACK_BUFSIZE;

    raw->buf = raw->stack_buf;
    raw->len = sizeof(raw->stack_buf);
    raw->heap_allocated = false;

    if (options->buffer_size_set) {
        selected = options->buffer_size;
    } else {
        struct stat st;
        if (fstat(STDOUT_FILENO, &st) == 0) {
            size_t blk = (size_t)st.st_blksize;
            if (blk > selected) {
                selected = blk;
            }
        }
    }

    if (selected > sizeof(raw->stack_buf)) {
        void *mem = cat_sys_malloc(selected);
        if (mem == NULL) {
            fprintf(stderr,
                    "%s: warning: unable to allocate %lu-byte buffer, using %lu-byte fallback\n",
                    options->progname,
                    (unsigned long)selected,
                    (unsigned long)sizeof(raw->stack_buf));
            raw->buf = raw->stack_buf;
            raw->len = sizeof(raw->stack_buf);
            return 0;
        }

        raw->buf = (unsigned char *)mem;
        raw->len = selected;
        raw->heap_allocated = true;
        return 0;
    }

    raw->len = selected;
    return 0;
}

static void cat_raw_buffer_destroy(struct cat_raw_buffer *raw)
{
    if (raw->heap_allocated) {
        free(raw->buf);
    }
    raw->buf = raw->stack_buf;
    raw->len = sizeof(raw->stack_buf);
    raw->heap_allocated = false;
}

static void cat_maybe_clear_nonblock(int fd, const struct cat_options *options, const char *name)
{
    int flags;

    for (;;) {
        flags = cat_sys_fcntl_getfl(fd);
        if (flags >= 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        cat_warn_file(options, name);
        return;
    }

    if ((flags & O_NONBLOCK) == 0) {
        return;
    }

    for (;;) {
        if (cat_sys_fcntl_setfl(fd, flags & ~O_NONBLOCK) == 0) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }

        cat_warn_file(options, name);
        return;
    }
}

static int cat_process_raw_fd(int fd,
                              const char *name,
                              const struct cat_options *options,
                              struct cat_raw_buffer *raw)
{
    for (;;) {
        ssize_t n = cat_sys_read(fd, raw->buf, raw->len);

        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            cat_warn_file(options, name);
            return 1;
        }

        if (cat_write_all(raw->buf, (size_t)n) < 0) {
            cat_warn_stdout(options);
            return -1;
        }
    }
}

static int cat_process_raw_file(const char *name,
                                const struct cat_options *options,
                                struct cat_raw_buffer *raw)
{
    int fd;
    int open_flags = 0;
    int rc;

    if (cat_is_stdin_name(name)) {
        return cat_process_raw_fd(STDIN_FILENO, "stdin", options, raw);
    }

    if (options->fast_open) {
        open_flags |= O_NONBLOCK;
    }

    fd = cat_sys_open_ro(name, open_flags);
    if (fd < 0) {
        cat_warn_file(options, name);
        return 1;
    }

    if (options->fast_open) {
        cat_maybe_clear_nonblock(fd, options, name);
    }

    rc = cat_process_raw_fd(fd, name, options, raw);
    if (close(fd) < 0) {
        cat_warn_file(options, name);
        if (rc == 0) {
            rc = 1;
        }
    }

    return rc;
}

static int cat_process_cooked_stream(FILE *fp,
                                     const struct cat_options *options,
                                     const char *name,
                                     const struct cat_cooked_cfg *cfg,
                                     struct cat_cooked_state *state,
                                     struct cat_sink *sink)
{
    for (;;) {
        int ch = fgetc(fp);

        if (ch == EOF) {
            if (ferror(fp)) {
                if (errno == EINTR) {
                    clearerr(fp);
                    continue;
                }
                cat_warn_file(options, name);
                return 1;
            }
            break;
        }

        {
            unsigned char byte = (unsigned char)ch;
            if (cat_cooked_process(&byte, 1, cfg, state, cat_sink_emit, sink) < 0) {
                cat_warn_stdout(options);
                return -1;
            }
        }
    }

    if (cat_sink_flush(sink) < 0) {
        cat_warn_stdout(options);
        return -1;
    }

    return 0;
}

static int cat_process_cooked_file(const char *name,
                                   const struct cat_options *options,
                                   const struct cat_cooked_cfg *cfg,
                                   struct cat_cooked_state *state,
                                   struct cat_sink *sink)
{
    FILE *fp;
    int rc;

    if (cat_is_stdin_name(name)) {
        return cat_process_cooked_stream(stdin, options, "stdin", cfg, state, sink);
    }

    fp = fopen(name, "rb");
    if (fp == NULL) {
        cat_warn_file(options, name);
        return 1;
    }

    rc = cat_process_cooked_stream(fp, options, name, cfg, state, sink);
    if (fclose(fp) != 0) {
        cat_warn_file(options, name);
        if (rc == 0) {
            rc = 1;
        }
    }

    return rc;
}

int main(int argc, char **argv)
{
    struct cat_options options;
    int status = 0;
    bool locked = false;
    bool cooked_mode;

    memset(&options, 0, sizeof(options));
    options.progname = (argv[0] != NULL && argv[0][0] != '\0') ? argv[0] : "cat";

    if (cat_parse_options(argc, argv, &options) < 0) {
        return 1;
    }

    if (options.unbuffered_stdout) {
        setbuf(stdout, NULL);
    }

    cooked_mode = cat_use_cooked_mode(&options);

    if (options.lock_stdout) {
        if (cat_lock_stdout_fd(&options) < 0) {
            return 1;
        }
        locked = true;
    }

    if (cooked_mode) {
        struct cat_cooked_cfg cfg;
        struct cat_cooked_state cooked_state;
        struct cat_sink sink;
        int i;

        memset(&cfg, 0, sizeof(cfg));
        cfg.number_all = options.number_all;
        cfg.number_nonblank = options.number_nonblank;
        cfg.squeeze_blank = options.squeeze_blank;
        cfg.show_ends = options.show_ends;
        cfg.show_tabs = options.show_tabs;
        cfg.show_nonprint = options.show_nonprint;

        cat_cooked_state_init(&cooked_state);
        sink.fd = STDOUT_FILENO;
        sink.used = 0;

        if (optind >= argc) {
            int rc = cat_process_cooked_file("-", &options, &cfg, &cooked_state, &sink);
            if (rc < 0) {
                status = 1;
                goto out;
            }
            if (rc > 0) {
                status = 1;
            }
        } else {
            for (i = optind; i < argc; i++) {
                int rc = cat_process_cooked_file(argv[i], &options, &cfg, &cooked_state, &sink);
                if (rc < 0) {
                    status = 1;
                    goto out;
                }
                if (rc > 0) {
                    status = 1;
                }
            }
        }

        if (cat_sink_flush(&sink) < 0) {
            cat_warn_stdout(&options);
            status = 1;
            goto out;
        }
    } else {
        struct cat_raw_buffer raw;
        int i;

        if (cat_raw_buffer_init(&raw, &options) < 0) {
            status = 1;
            goto out;
        }

        if (optind >= argc) {
            int rc = cat_process_raw_file("-", &options, &raw);
            if (rc < 0) {
                status = 1;
                cat_raw_buffer_destroy(&raw);
                goto out;
            }
            if (rc > 0) {
                status = 1;
            }
        } else {
            for (i = optind; i < argc; i++) {
                int rc = cat_process_raw_file(argv[i], &options, &raw);
                if (rc < 0) {
                    status = 1;
                    cat_raw_buffer_destroy(&raw);
                    goto out;
                }
                if (rc > 0) {
                    status = 1;
                }
            }
        }

        cat_raw_buffer_destroy(&raw);
    }

out:
    if (locked && cat_unlock_stdout_fd(&options) < 0) {
        status = 1;
    }

    return status;
}
