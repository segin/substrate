#include <assert.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "ls.h"
#include "ls_sort.h"
#include "ls_traverse.h"

static char *join_path2(const char *a, const char *b) {
    size_t al = strlen(a);
    size_t bl = strlen(b);
    size_t need = al + 1 + bl + 1;
    char *out = (char *)malloc(need);
    if (out == NULL) {
        return NULL;
    }
    if (al > 0 && a[al - 1] == '/') {
        snprintf(out, need, "%s%s", a, b);
    } else {
        snprintf(out, need, "%s/%s", a, b);
    }
    return out;
}

static void write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "wb");
    assert(fp != NULL);
    if (content != NULL) {
        size_t n = fwrite(content, 1, strlen(content), fp);
        assert(n == strlen(content));
    }
    fclose(fp);
}

static char *mkdtemp_with_prefix(const char *prefix) {
    const char *tmpbase = getenv("TMPDIR");
    size_t n;
    char *tpl;

    if (!tmpbase || !*tmpbase) {
        tmpbase = "/tmp";
    }

    n = strlen(tmpbase) + 1 + strlen(prefix) + 6 + 1;
    tpl = (char *)malloc(n);
    assert(tpl != NULL);
    snprintf(tpl, n, "%s/%sXXXXXX", tmpbase, prefix);
    assert(mkdtemp(tpl) != NULL);
    return tpl;
}

static void create_unix_socket(const char *path, int *fd_out) {
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    assert(strlen(path) < sizeof(addr.sun_path));
    strcpy(addr.sun_path, path);

    unlink(path);
    assert(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    *fd_out = fd;
}

static char *slurp_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long sz;
    char *buf;
    size_t n;

    assert(fp != NULL);
    assert(fseek(fp, 0, SEEK_END) == 0);
    sz = ftell(fp);
    assert(sz >= 0);
    assert(fseek(fp, 0, SEEK_SET) == 0);

    buf = (char *)malloc((size_t)sz + 1);
    assert(buf != NULL);
    n = fread(buf, 1, (size_t)sz, fp);
    assert(n == (size_t)sz);
    buf[n] = '\0';
    fclose(fp);
    return buf;
}

static int run_ls_capture(const ls_config_t *cfg, char **paths, int npaths,
                          char **out_text, char **err_text) {
    const char *tmpbase = getenv("TMPDIR");
    char *out_tpl;
    char *err_tpl;
    int out_fd;
    int err_fd;
    int saved_out;
    int saved_err;
    int rc;

    if (!tmpbase || !*tmpbase) {
        tmpbase = "/tmp";
    }

    out_tpl = (char *)malloc(strlen(tmpbase) + 1 + strlen("ls_out_XXXXXX") + 1);
    err_tpl = (char *)malloc(strlen(tmpbase) + 1 + strlen("ls_err_XXXXXX") + 1);
    assert(out_tpl != NULL && err_tpl != NULL);

    sprintf(out_tpl, "%s/%s", tmpbase, "ls_out_XXXXXX");
    sprintf(err_tpl, "%s/%s", tmpbase, "ls_err_XXXXXX");

    out_fd = mkstemp(out_tpl);
    err_fd = mkstemp(err_tpl);
    assert(out_fd >= 0 && err_fd >= 0);

    saved_out = dup(STDOUT_FILENO);
    saved_err = dup(STDERR_FILENO);
    assert(saved_out >= 0 && saved_err >= 0);

    fflush(stdout);
    fflush(stderr);
    assert(dup2(out_fd, STDOUT_FILENO) >= 0);
    assert(dup2(err_fd, STDERR_FILENO) >= 0);

    close(out_fd);
    close(err_fd);

    rc = ls_run(cfg, paths, npaths);

    fflush(stdout);
    fflush(stderr);
    assert(dup2(saved_out, STDOUT_FILENO) >= 0);
    assert(dup2(saved_err, STDERR_FILENO) >= 0);
    close(saved_out);
    close(saved_err);

    *out_text = slurp_file(out_tpl);
    *err_text = slurp_file(err_tpl);

    unlink(out_tpl);
    unlink(err_tpl);
    free(out_tpl);
    free(err_tpl);
    return rc;
}

static const char *find_line_for(const char *text, const char *needle) {
    const char *p = text;
    size_t nlen = strlen(needle);

    while (p != NULL && *p != '\0') {
        const char *line_end = strchr(p, '\n');
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (line_len >= nlen) {
            size_t i;
            for (i = 0; i + nlen <= line_len; i++) {
                if (memcmp(p + i, needle, nlen) == 0) {
                    return p;
                }
            }
        }
        if (line_end == NULL) {
            break;
        }
        p = line_end + 1;
    }
    return NULL;
}

static int line_index_of(const char *text, const char *needle) {
    const char *p = text;
    size_t nlen = strlen(needle);
    int idx = 0;

    while (p != NULL && *p != '\0') {
        const char *line_end = strchr(p, '\n');
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (line_len == nlen && memcmp(p, needle, nlen) == 0) {
            return idx;
        }
        if (line_end == NULL) {
            break;
        }
        p = line_end + 1;
        idx++;
    }

    return -1;
}

static int line_index_contains(const char *text, const char *needle) {
    const char *p = text;
    size_t nlen = strlen(needle);
    int idx = 0;

    while (p != NULL && *p != '\0') {
        const char *line_end = strchr(p, '\n');
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (line_len >= nlen) {
            size_t i;
            for (i = 0; i + nlen <= line_len; i++) {
                if (memcmp(p + i, needle, nlen) == 0) {
                    return idx;
                }
            }
        }
        if (line_end == NULL) {
            break;
        }
        p = line_end + 1;
        idx++;
    }

    return -1;
}

static void init_cfg(ls_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->color = LS_COLOR_NEVER;
    cfg->time_type = TIME_MTIME;
    cfg->time_style = TIME_STYLE_LOCALE;
    cfg->quoting_style = LS_QUOTE_LITERAL;
}

static void test_sorting_modes(void) {
    char *tmp;
    char *dir;
    char *a;
    char *b;
    char *c;
    ls_config_t cfg;
    char *out;
    char *err;
    char *argv[1];
    struct timespec ts[2];

    tmp = mkdtemp_with_prefix("ls_sort_");
    dir = tmp;

    a = join_path2(dir, "a.txt");
    b = join_path2(dir, "b.txt");
    c = join_path2(dir, "c.txt");
    assert(a && b && c);

    write_file(a, "1234");
    write_file(b, "1");
    write_file(c, "12");

    ts[0].tv_nsec = 0;
    ts[1].tv_nsec = 0;
    ts[0].tv_sec = 1700000100;
    ts[1].tv_sec = 1700000100;
    assert(utimensat(AT_FDCWD, a, ts, 0) == 0);
    ts[0].tv_sec = 1700000200;
    ts[1].tv_sec = 1700000200;
    assert(utimensat(AT_FDCWD, b, ts, 0) == 0);
    ts[0].tv_sec = 1700000000;
    ts[1].tv_sec = 1700000000;
    assert(utimensat(AT_FDCWD, c, ts, 0) == 0);

    init_cfg(&cfg);
    cfg.one_per_line = true;
    argv[0] = dir;

    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(line_index_of(out, "a.txt") >= 0);
    assert(line_index_of(out, "b.txt") >= 0);
    assert(line_index_of(out, "c.txt") >= 0);
    assert(line_index_of(out, "a.txt") < line_index_of(out, "b.txt"));
    assert(line_index_of(out, "b.txt") < line_index_of(out, "c.txt"));
    free(out);
    free(err);

    cfg.sort_size = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(line_index_of(out, "a.txt") < line_index_of(out, "c.txt"));
    assert(line_index_of(out, "c.txt") < line_index_of(out, "b.txt"));
    cfg.sort_size = false;
    free(out);
    free(err);

    cfg.sort_time = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(line_index_of(out, "b.txt") < line_index_of(out, "a.txt"));
    assert(line_index_of(out, "a.txt") < line_index_of(out, "c.txt"));
    free(out);
    free(err);

    unlink(a);
    unlink(b);
    unlink(c);
    rmdir(dir);
    free(tmp);
    free(a);
    free(b);
    free(c);

    printf("PASS: test_sorting_modes\n");
}

static void test_output_modes(void) {
    char *tmp;
    char *dir;
    char *f1;
    char *f2;
    char *f3;
    ls_config_t cfg;
    char *out;
    char *err;
    char *argv[1];

    tmp = mkdtemp_with_prefix("ls_modes_");
    dir = tmp;
    f1 = join_path2(dir, "alpha");
    f2 = join_path2(dir, "beta");
    f3 = join_path2(dir, "gamma");
    assert(f1 && f2 && f3);

    write_file(f1, "a");
    write_file(f2, "b");
    write_file(f3, "c");

    argv[0] = dir;

    init_cfg(&cfg);
    cfg.one_per_line = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(strstr(out, "alpha\n") != NULL);
    assert(strstr(out, "beta\n") != NULL);
    assert(strstr(out, "gamma\n") != NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.comma_sep = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(strstr(out, ", ") != NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.multi_column = true;
    cfg.term_width = 40;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(strstr(out, "alpha") != NULL);
    assert(strstr(out, "beta") != NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.by_lines = true;
    cfg.multi_column = true;
    cfg.term_width = 40;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(strstr(out, "alpha") != NULL);
    assert(strstr(out, "beta") != NULL);
    free(out);
    free(err);

    unlink(f1);
    unlink(f2);
    unlink(f3);
    rmdir(dir);
    free(tmp);
    free(f1);
    free(f2);
    free(f3);

    printf("PASS: test_output_modes\n");
}

static void test_long_and_symlink(void) {
    char *tmp;
    char *dir;
    char *target;
    char *link_ok;
    char *link_bad;
    ls_config_t cfg;
    char *out;
    char *err;
    char *argv[1];

    tmp = mkdtemp_with_prefix("ls_long_");
    dir = tmp;

    target = join_path2(dir, "target");
    link_ok = join_path2(dir, "ok");
    link_bad = join_path2(dir, "dangling");
    assert(target && link_ok && link_bad);

    write_file(target, "payload");
    assert(symlink("target", link_ok) == 0);
    assert(symlink("missing", link_bad) == 0);

    argv[0] = dir;

    init_cfg(&cfg);
    cfg.long_fmt = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(strstr(out, " -> target") != NULL);
    assert(strstr(out, "dangling") != NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.long_fmt = true;
    cfg.dereference = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, "[dangling]") != NULL);
    free(out);
    free(err);

    unlink(link_ok);
    unlink(link_bad);
    unlink(target);
    rmdir(dir);
    free(tmp);
    free(target);
    free(link_ok);
    free(link_bad);

    printf("PASS: test_long_and_symlink\n");
}

static void test_recursive_cycle_detection(void) {
    char *tmp;
    char *dir;
    char *sub;
    char *file;
    char *loop;
    ls_config_t cfg;
    char *out;
    char *err;
    char *argv[1];
    int rc;

    tmp = mkdtemp_with_prefix("ls_rec_");
    dir = tmp;
    sub = join_path2(dir, "sub");
    assert(sub != NULL);
    assert(mkdir(sub, 0755) == 0);

    file = join_path2(sub, "file");
    loop = join_path2(sub, "loop");
    assert(file && loop);

    write_file(file, "x");
    assert(symlink("..", loop) == 0);

    argv[0] = dir;
    init_cfg(&cfg);
    cfg.recursive = true;
    cfg.dereference = true;

    rc = run_ls_capture(&cfg, argv, 1, &out, &err);
    assert(rc == LS_EXIT_MINOR);
    assert(strstr(out, "sub:") != NULL);
    assert(strstr(err, "filesystem loop detected") != NULL);

    free(out);
    free(err);

    unlink(loop);
    unlink(file);
    rmdir(sub);
    rmdir(dir);
    free(tmp);
    free(sub);
    free(file);
    free(loop);

    printf("PASS: test_recursive_cycle_detection\n");
}

static void test_acceptance_basics(void) {
    char *tmp;
    char *dir;
    char *sub;
    char *a;
    char *b;
    char *hidden;
    char *out;
    char *err;
    char *argv[1];
    ls_config_t cfg;
    struct timespec ts[2];
    char *empty_tpl;
    char *empty_dir;
    char *pair[2];

    tmp = mkdtemp_with_prefix("ls_acc_");
    dir = tmp;
    empty_tpl = mkdtemp_with_prefix("ls_empty_");
    empty_dir = empty_tpl;

    sub = join_path2(dir, "sub");
    a = join_path2(dir, "a");
    b = join_path2(dir, "b");
    hidden = join_path2(dir, ".hidden");
    assert(sub && a && b && hidden);

    assert(mkdir(sub, 0755) == 0);
    write_file(a, "1234567890");
    write_file(b, "x");
    write_file(hidden, "h");

    ts[0].tv_nsec = 0;
    ts[1].tv_nsec = 0;
    ts[0].tv_sec = 1700001000;
    ts[1].tv_sec = 1700001000;
    assert(utimensat(AT_FDCWD, a, ts, 0) == 0);
    ts[0].tv_sec = 1700002000;
    ts[1].tv_sec = 1700002000;
    assert(utimensat(AT_FDCWD, b, ts, 0) == 0);

    argv[0] = dir;

    init_cfg(&cfg);
    cfg.one_per_line = true;
    argv[0] = empty_dir;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(out[0] == '\0');
    free(out);
    free(err);

    argv[0] = dir;

    init_cfg(&cfg);
    cfg.one_per_line = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, ".hidden") == NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.one_per_line = true;
    cfg.all = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, ".\n") != NULL);
    assert(strstr(out, "..\n") != NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.long_fmt = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(out[0] != '\0');
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.long_fmt = true;
    cfg.human_readable = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, "K") != NULL || strstr(out, "M") != NULL || strstr(out, "10") != NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.recursive = true;
    cfg.one_per_line = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, "sub:\n") != NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.long_fmt = true;
    cfg.sort_size = true;
    pair[0] = a;
    pair[1] = b;
    assert(run_ls_capture(&cfg, pair, 2, &out, &err) == LS_EXIT_OK);
    assert(line_index_contains(out, a) < line_index_contains(out, b));
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.long_fmt = true;
    cfg.sort_time = true;
    assert(run_ls_capture(&cfg, pair, 2, &out, &err) == LS_EXIT_OK);
    assert(line_index_contains(out, b) < line_index_contains(out, a));
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.classify = true;
    cfg.one_per_line = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, "sub/") != NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.color = LS_COLOR_ALWAYS;
    cfg.one_per_line = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, "\033[") != NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.inode = true;
    cfg.one_per_line = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(out[0] >= '0' && out[0] <= '9');
    free(out);
    free(err);

    unlink(a);
    unlink(b);
    unlink(hidden);
    rmdir(sub);
    rmdir(dir);
    rmdir(empty_dir);
    free(tmp);
    free(empty_tpl);
    free(sub);
    free(a);
    free(b);
    free(hidden);

    printf("PASS: test_acceptance_basics\n");
}

static void test_extended_sorting_flags(void) {
    char *tmp;
    char *dir;
    char *f_a2;
    char *f_a10;
    char *f_caps;
    char *f_c;
    char *f_h;
    char *d_sub;
    char *out;
    char *err;
    char *argv[1];
    ls_config_t cfg;

    tmp = mkdtemp_with_prefix("ls_sort_ext_");
    dir = tmp;

    f_a2 = join_path2(dir, "a2.txt");
    f_a10 = join_path2(dir, "a10.txt");
    f_caps = join_path2(dir, "B.txt");
    f_c = join_path2(dir, "x.c");
    f_h = join_path2(dir, "y.h");
    d_sub = join_path2(dir, "subdir");
    assert(f_a2 && f_a10 && f_caps && f_c && f_h && d_sub);

    write_file(f_a2, "a2");
    write_file(f_a10, "a10");
    write_file(f_caps, "B");
    write_file(f_c, "c");
    write_file(f_h, "h");
    assert(mkdir(d_sub, 0755) == 0);

    argv[0] = dir;

    init_cfg(&cfg);
    cfg.one_per_line = true;
    cfg.version_sort = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(line_index_of(out, "a2.txt") < line_index_of(out, "a10.txt"));
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.one_per_line = true;
    cfg.sort_ignore_case = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(line_index_of(out, "a10.txt") < line_index_of(out, "B.txt"));
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.one_per_line = true;
    cfg.sort_extension = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(line_index_of(out, "x.c") < line_index_of(out, "y.h"));
    assert(line_index_of(out, "y.h") < line_index_of(out, "a2.txt"));
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.one_per_line = true;
    cfg.dirs_first = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(line_index_of(out, "subdir") < line_index_of(out, "a10.txt"));
    free(out);
    free(err);

    unlink(f_a2);
    unlink(f_a10);
    unlink(f_caps);
    unlink(f_c);
    unlink(f_h);
    rmdir(d_sub);
    rmdir(dir);
    free(tmp);
    free(f_a2);
    free(f_a10);
    free(f_caps);
    free(f_c);
    free(f_h);
    free(d_sub);

    printf("PASS: test_extended_sorting_flags\n");
}

static void test_special_files_and_permission_denied(void) {
    char *tmp;
    char *dir;
    char *fifo_path;
    char *sock_path;
    char *deny_dir;
    char *deny_file;
    char *out;
    char *err;
    char *argv[1];
    ls_config_t cfg;
    int sfd = -1;

    tmp = mkdtemp_with_prefix("ls_special_");
    dir = tmp;

    fifo_path = join_path2(dir, "pipe");
    sock_path = join_path2(dir, "sock");
    deny_dir = join_path2(dir, "denied");
    deny_file = join_path2(deny_dir, "hidden");
    assert(fifo_path && sock_path && deny_dir && deny_file);

    assert(mkfifo(fifo_path, 0644) == 0);
    create_unix_socket(sock_path, &sfd);
    assert(mkdir(deny_dir, 0700) == 0);
    write_file(deny_file, "secret");
    assert(chmod(deny_dir, 0000) == 0);

    argv[0] = dir;

    init_cfg(&cfg);
    cfg.long_fmt = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, "pipe") != NULL);
    assert(strstr(out, "sock") != NULL);
    {
        const char *line_fifo = find_line_for(out, "pipe");
        const char *line_sock = find_line_for(out, "sock");
        assert(line_fifo != NULL && line_sock != NULL);
        assert(line_fifo[0] == 'p');
        assert(line_sock[0] == 's');
    }
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.recursive = true;
    cfg.one_per_line = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_MINOR);
    assert(strstr(err, "cannot open directory") != NULL);
    free(out);
    free(err);

    assert(chmod(deny_dir, 0700) == 0);
    close(sfd);
    unlink(sock_path);
    unlink(fifo_path);
    unlink(deny_file);
    rmdir(deny_dir);
    rmdir(dir);
    free(tmp);
    free(fifo_path);
    free(sock_path);
    free(deny_dir);
    free(deny_file);

    printf("PASS: test_special_files_and_permission_denied\n");
}

static void test_recursive_one_file_system(void) {
    char *tmp;
    char *dir;
    char *local;
    char *foreign;
    char *local_file;
    char *foreign_hdr;
    char *out;
    char *err;
    char *argv[1];
    ls_config_t cfg;
    struct stat st_root;
    struct stat st_shm;

    if (stat("/dev/shm", &st_shm) != 0) {
        printf("PASS: test_recursive_one_file_system (skipped: no /dev/shm)\n");
        return;
    }

    tmp = mkdtemp_with_prefix("ls_onefs_");
    dir = tmp;
    assert(stat(dir, &st_root) == 0);
    if (st_root.st_dev == st_shm.st_dev) {
        rmdir(dir);
        free(tmp);
        printf("PASS: test_recursive_one_file_system (skipped: same filesystem)\n");
        return;
    }

    local = join_path2(dir, "local");
    foreign = join_path2(dir, "foreign");
    assert(local && foreign);

    assert(mkdir(local, 0755) == 0);
    local_file = join_path2(local, "ok");
    assert(local_file != NULL);
    write_file(local_file, "ok");

    unlink(foreign);
    assert(symlink("/dev/shm", foreign) == 0);

    argv[0] = dir;
    init_cfg(&cfg);
    cfg.recursive = true;
    cfg.dereference = true;
    cfg.one_file_system = true;
    cfg.one_per_line = true;

    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, "local:") != NULL);

    foreign_hdr = (char *)malloc(strlen(foreign) + 3);
    assert(foreign_hdr != NULL);
    sprintf(foreign_hdr, "%s:\n", foreign);
    assert(strstr(out, foreign_hdr) == NULL);
    free(foreign_hdr);

    free(out);
    free(err);

    unlink(local_file);
    unlink(foreign);
    assert(rmdir(local) == 0);
    assert(rmdir(dir) == 0);
    free(tmp);
    free(local_file);
    free(local);
    free(foreign);

    printf("PASS: test_recursive_one_file_system\n");
}

static void test_machine_parse_and_unicode_width(void) {
    char *tmp;
    char *dir;
    char *n_space;
    char *n_quote;
    char *n_nl;
    char *n_utf8;
    char *n_combining;
    char *out;
    char *err;
    char *argv[1];
    ls_config_t cfg;
    const char utf8_name[] = "\xE4\xB8\xAD\xE6\x96\x87.txt";
    const char combining_name[] = "e\xCC\x81.txt";

    tmp = mkdtemp_with_prefix("ls_parse_");
    dir = tmp;

    n_space = join_path2(dir, "space name");
    n_quote = join_path2(dir, "quote\"name");
    n_nl = join_path2(dir, "line\nbreak");
    n_utf8 = join_path2(dir, utf8_name);
    n_combining = join_path2(dir, combining_name);
    assert(n_space && n_quote && n_nl && n_utf8 && n_combining);

    write_file(n_space, "1");
    write_file(n_quote, "2");
    write_file(n_nl, "3");
    write_file(n_utf8, "4");
    write_file(n_combining, "5");

    argv[0] = dir;
    init_cfg(&cfg);
    cfg.one_per_line = true;
    cfg.quoting_style = LS_QUOTE_ESCAPE;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, "space\\ name") != NULL);
    assert(strstr(out, "quote\\\"name") != NULL);
    assert(strstr(out, "line\\nbreak") != NULL);
    assert(strstr(out, "\n\n") == NULL);
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.multi_column = true;
    cfg.term_width = 24;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, utf8_name) != NULL);
    assert(strstr(out, combining_name) != NULL);
    free(out);
    free(err);

    unlink(n_space);
    unlink(n_quote);
    unlink(n_nl);
    unlink(n_utf8);
    unlink(n_combining);
    rmdir(dir);
    free(tmp);
    free(n_space);
    free(n_quote);
    free(n_nl);
    free(n_utf8);
    free(n_combining);

    printf("PASS: test_machine_parse_and_unicode_width\n");
}

static void test_ls_sort_entries_unit(void) {
    file_info_t files[5];
    ls_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.time_type = TIME_MTIME;

    memset(files, 0, sizeof(files));

    // File 0: A directory, largest size, oldest mtime
    files[0].name = "zebra";
    files[0].st.st_mode = S_IFDIR;
    files[0].st.st_size = 1000;
    files[0].st.st_mtime = 100;
    files[0].input_index = 0;

    // File 1: A regular file, small size, middle mtime, .c extension
    files[1].name = "apple.c";
    files[1].st.st_mode = S_IFREG;
    files[1].st.st_size = 10;
    files[1].st.st_mtime = 200;
    files[1].input_index = 1;

    // File 2: A regular file, medium size, newest mtime, no extension
    files[2].name = "banana";
    files[2].st.st_mode = S_IFREG;
    files[2].st.st_size = 100;
    files[2].st.st_mtime = 300;
    files[2].input_index = 2;

    // File 3: A regular file, version 2
    files[3].name = "file2.txt";
    files[3].st.st_mode = S_IFREG;
    files[3].st.st_size = 50;
    files[3].st.st_mtime = 150;
    files[3].input_index = 3;

    // File 4: A regular file, version 10
    files[4].name = "file10.txt";
    files[4].st.st_mode = S_IFREG;
    files[4].st.st_size = 50;
    files[4].st.st_mtime = 150;
    files[4].input_index = 4;

    // 1. Default sort (alphabetical)
    ls_sort_entries(files, 5, &cfg);
    assert(strcmp(files[0].name, "apple.c") == 0);
    assert(strcmp(files[1].name, "banana") == 0);
    assert(strcmp(files[2].name, "file10.txt") == 0);
    assert(strcmp(files[3].name, "file2.txt") == 0);
    assert(strcmp(files[4].name, "zebra") == 0);

    // 2. Sort size
    cfg.sort_size = true;
    ls_sort_entries(files, 5, &cfg);
    assert(strcmp(files[0].name, "zebra") == 0);
    assert(strcmp(files[1].name, "banana") == 0);
    // file10.txt and file2.txt have same size. alphabetical fallback.
    assert(strcmp(files[2].name, "file10.txt") == 0);
    assert(strcmp(files[3].name, "file2.txt") == 0);
    assert(strcmp(files[4].name, "apple.c") == 0);
    cfg.sort_size = false;

    // 3. Sort time
    cfg.sort_time = true;
    ls_sort_entries(files, 5, &cfg);
    // Newest first
    assert(strcmp(files[0].name, "banana") == 0);
    assert(strcmp(files[1].name, "apple.c") == 0);
    // file10.txt and file2.txt have same time. alphabetical fallback.
    assert(strcmp(files[2].name, "file10.txt") == 0);
    assert(strcmp(files[3].name, "file2.txt") == 0);
    assert(strcmp(files[4].name, "zebra") == 0);
    cfg.sort_time = false;

    // 4. Version sort
    cfg.version_sort = true;
    ls_sort_entries(files, 5, &cfg);
    assert(strcmp(files[0].name, "apple.c") == 0);
    assert(strcmp(files[1].name, "banana") == 0);
    assert(strcmp(files[2].name, "file2.txt") == 0);
    assert(strcmp(files[3].name, "file10.txt") == 0);
    assert(strcmp(files[4].name, "zebra") == 0);
    cfg.version_sort = false;

    // 5. Extension sort
    cfg.sort_extension = true;
    ls_sort_entries(files, 5, &cfg);
    // no extension (banana, zebra)
    assert(strcmp(files[0].name, "banana") == 0);
    assert(strcmp(files[1].name, "zebra") == 0);
    // .c (apple.c)
    assert(strcmp(files[2].name, "apple.c") == 0);
    // .txt (file10.txt, file2.txt)
    assert(strcmp(files[3].name, "file10.txt") == 0);
    assert(strcmp(files[4].name, "file2.txt") == 0);
    cfg.sort_extension = false;

    // 6. Dirs first
    cfg.dirs_first = true;
    ls_sort_entries(files, 5, &cfg);
    assert(strcmp(files[0].name, "zebra") == 0);
    assert(strcmp(files[1].name, "apple.c") == 0);
    assert(strcmp(files[2].name, "banana") == 0);
    assert(strcmp(files[3].name, "file10.txt") == 0);
    assert(strcmp(files[4].name, "file2.txt") == 0);
    cfg.dirs_first = false;

    // 7. Reverse sort
    cfg.reverse = true;
    ls_sort_entries(files, 5, &cfg);
    assert(strcmp(files[0].name, "zebra") == 0);
    assert(strcmp(files[1].name, "file2.txt") == 0);
    assert(strcmp(files[2].name, "file10.txt") == 0);
    assert(strcmp(files[3].name, "banana") == 0);
    assert(strcmp(files[4].name, "apple.c") == 0);
    cfg.reverse = false;

    // 8. No sort
    cfg.no_sort = true;
    files[0].name = "zebra"; files[0].input_index = 0;
    files[1].name = "apple.c"; files[1].input_index = 1;
    files[2].name = "banana"; files[2].input_index = 2;
    files[3].name = "file2.txt"; files[3].input_index = 3;
    files[4].name = "file10.txt"; files[4].input_index = 4;
    ls_sort_entries(files, 5, &cfg);
    assert(strcmp(files[0].name, "zebra") == 0);
    assert(strcmp(files[1].name, "apple.c") == 0);
    assert(strcmp(files[2].name, "banana") == 0);
    assert(strcmp(files[3].name, "file2.txt") == 0);
    assert(strcmp(files[4].name, "file10.txt") == 0);
    cfg.no_sort = false;

    // 9. Ignore case
    cfg.sort_ignore_case = true;
    files[0].name = "Zebra";
    files[1].name = "apple.c";
    files[2].name = "Banana";
    files[3].name = "file2.txt";
    files[4].name = "File10.txt";
    ls_sort_entries(files, 5, &cfg);
    assert(strcmp(files[0].name, "apple.c") == 0);
    assert(strcmp(files[1].name, "Banana") == 0);
    assert(strcmp(files[2].name, "File10.txt") == 0);
    assert(strcmp(files[3].name, "file2.txt") == 0);
    assert(strcmp(files[4].name, "Zebra") == 0);
    cfg.sort_ignore_case = false;

    printf("PASS: test_ls_sort_entries_unit\n");
}

int main(void) {
    (void)setlocale(LC_ALL, "");
    test_ls_sort_entries_unit();
    test_sorting_modes();
    test_output_modes();
    test_long_and_symlink();
    test_recursive_cycle_detection();
    test_acceptance_basics();
    test_extended_sorting_flags();
    test_special_files_and_permission_denied();
    test_recursive_one_file_system();
    test_machine_parse_and_unicode_width();
    return 0;
}
