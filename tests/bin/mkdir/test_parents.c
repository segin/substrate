
#include "mkdir.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static int
mode_of(const char *path, mode_t *out_mode)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return -1;
    }
    *out_mode = st.st_mode & 07777;
    return 0;
}

static int
test_parent_creation(void)
{
    struct mkdir_options opts;
    char *err_path = NULL;
    int err_no = 0;

    mkdir_options_init(&opts, "mkdir");
    opts.parents = true;
    CHECK(mkdir_create_parents(&opts, "a//b///c/", 0777, false, 0777,
        &err_path, &err_no) == 0);
    CHECK(access("a", F_OK) == 0);
    CHECK(access("a/b", F_OK) == 0);
    CHECK(access("a/b/c", F_OK) == 0);
    free(err_path);
    return 0;
}

static int
test_relative_components(void)
{
    struct mkdir_options opts;
    char *err_path = NULL;
    int err_no = 0;

    mkdir_options_init(&opts, "mkdir");
    opts.parents = true;

    CHECK(mkdir("base", 0755) == 0);
    CHECK(mkdir_create_parents(&opts, "base/../side", 0777, false, 0777,
        &err_path, &err_no) == 0);
    CHECK(access("side", F_OK) == 0);
    free(err_path);
    return 0;
}

static int
test_mode_application_and_existing_dir(void)
{
    struct mkdir_options opts;
    char *err_path = NULL;
    int err_no = 0;
    mode_t current_mode = 0;
    mode_t old_umask;

    mkdir_options_init(&opts, "mkdir");
    opts.parents = true;

    old_umask = umask(0022);
    CHECK(mkdir_create_parents(&opts, "modes/final", 0777, true, 0700,
        &err_path, &err_no) == 0);
    CHECK(mode_of("modes/final", &current_mode) == 0);
    CHECK(current_mode == 0700);
    (void)umask(old_umask);

    CHECK(mkdir("existing", 0755) == 0);
    CHECK(mkdir_create_parents(&opts, "existing", 0777, true, 0700,
        &err_path, &err_no) == 0);
    CHECK(mode_of("existing", &current_mode) == 0);
    CHECK(current_mode == 0755);
    free(err_path);
    return 0;
}

static int
test_file_component_error(void)
{
    struct mkdir_options opts;
    char *err_path = NULL;
    int err_no = 0;
    FILE *fp;

    mkdir_options_init(&opts, "mkdir");
    opts.parents = true;

    fp = fopen("file", "w");
    CHECK(fp != NULL);
    fclose(fp);

    CHECK(mkdir_create_parents(&opts, "file/sub", 0777, false, 0777,
        &err_path, &err_no) != 0);
    CHECK(err_no == ENOTDIR);
    free(err_path);
    return 0;
}

int
main(void)
{
    char template[] = "/tmp/mkdir-parents-XXXXXX";
    char *tmpdir;
    char *cwd;
    int rc = 0;

    tmpdir = mkdtemp(template);
    if (tmpdir == NULL) {
        perror("mkdtemp");
        return 1;
    }

    cwd = getcwd(NULL, 0);
    if (cwd == NULL) {
        perror("getcwd");
        return 1;
    }

    if (chdir(tmpdir) != 0) {
        perror("chdir");
        free(cwd);
        return 1;
    }

    if (test_parent_creation() != 0 || test_relative_components() != 0 ||
            test_mode_application_and_existing_dir() != 0 ||
            test_file_component_error() != 0) {
        rc = 1;
    }

    if (chdir(cwd) != 0) {
        perror("restore chdir");
        rc = 1;
    }
    free(cwd);
    printf("test_parents: %s\n", rc == 0 ? "ok" : "fail");
    return rc;
}