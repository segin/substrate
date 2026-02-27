#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "ls.h"
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
    char out_tpl[] = "/tmp/ls_out_XXXXXX";
    char err_tpl[] = "/tmp/ls_err_XXXXXX";
    int out_fd;
    int err_fd;
    int saved_out;
    int saved_err;
    int rc;

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
    return rc;
}

static void init_cfg(ls_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->color = LS_COLOR_NEVER;
    cfg->time_type = TIME_MTIME;
    cfg->time_style = TIME_STYLE_LOCALE;
    cfg->quoting_style = LS_QUOTE_LITERAL;
}

static void test_sorting_modes(void) {
    char tmp[] = "/tmp/ls_sort_XXXXXX";
    char *dir;
    char *a;
    char *b;
    char *c;
    ls_config_t cfg;
    char *out;
    char *err;
    char *argv[1];
    struct timespec ts[2];

    dir = mkdtemp(tmp);
    assert(dir != NULL);

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
    assert(strstr(out, "a.txt") < strstr(out, "b.txt"));
    assert(strstr(out, "b.txt") < strstr(out, "c.txt"));
    free(out);
    free(err);

    cfg.sort_size = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(strstr(out, "a.txt") < strstr(out, "c.txt"));
    assert(strstr(out, "c.txt") < strstr(out, "b.txt"));
    cfg.sort_size = false;
    free(out);
    free(err);

    cfg.sort_time = true;
    assert(run_ls_capture(&cfg, argv, 1, &out, &err) == 0);
    assert(strstr(out, "b.txt") < strstr(out, "a.txt"));
    assert(strstr(out, "a.txt") < strstr(out, "c.txt"));
    free(out);
    free(err);

    unlink(a);
    unlink(b);
    unlink(c);
    rmdir(dir);
    free(a);
    free(b);
    free(c);

    printf("PASS: test_sorting_modes\n");
}

static void test_output_modes(void) {
    char tmp[] = "/tmp/ls_modes_XXXXXX";
    char *dir;
    char *f1;
    char *f2;
    char *f3;
    ls_config_t cfg;
    char *out;
    char *err;
    char *argv[1];

    dir = mkdtemp(tmp);
    assert(dir != NULL);
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
    free(f1);
    free(f2);
    free(f3);

    printf("PASS: test_output_modes\n");
}

static void test_long_and_symlink(void) {
    char tmp[] = "/tmp/ls_long_XXXXXX";
    char *dir;
    char *target;
    char *link_ok;
    char *link_bad;
    ls_config_t cfg;
    char *out;
    char *err;
    char *argv[1];

    dir = mkdtemp(tmp);
    assert(dir != NULL);

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
    free(target);
    free(link_ok);
    free(link_bad);

    printf("PASS: test_long_and_symlink\n");
}

static void test_recursive_cycle_detection(void) {
    char tmp[] = "/tmp/ls_rec_XXXXXX";
    char *dir;
    char *sub;
    char *file;
    char *loop;
    ls_config_t cfg;
    char *out;
    char *err;
    char *argv[1];
    int rc;

    dir = mkdtemp(tmp);
    assert(dir != NULL);
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
    free(sub);
    free(file);
    free(loop);

    printf("PASS: test_recursive_cycle_detection\n");
}

static void test_acceptance_basics(void) {
    char tmp[] = "/tmp/ls_acc_XXXXXX";
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
    char empty_tpl[] = "/tmp/ls_empty_XXXXXX";
    char *empty_dir;
    char *pair[2];

    dir = mkdtemp(tmp);
    assert(dir != NULL);
    empty_dir = mkdtemp(empty_tpl);
    assert(empty_dir != NULL);

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
    assert(strstr(out, a) < strstr(out, b));
    free(out);
    free(err);

    init_cfg(&cfg);
    cfg.long_fmt = true;
    cfg.sort_time = true;
    assert(run_ls_capture(&cfg, pair, 2, &out, &err) == LS_EXIT_OK);
    assert(strstr(out, b) < strstr(out, a));
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
    free(sub);
    free(a);
    free(b);
    free(hidden);

    printf("PASS: test_acceptance_basics\n");
}

int main(void) {
    test_sorting_modes();
    test_output_modes();
    test_long_and_symlink();
    test_recursive_cycle_detection();
    test_acceptance_basics();
    return 0;
}
