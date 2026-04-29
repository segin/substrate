#include "rm_opts.h"
#include "rm_walk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int
create_file(const char *path)
{
    FILE *stream;

    stream = fopen(path, "w");
    if (stream == NULL) {
        return -1;
    }
    fputs("x", stream);
    fclose(stream);
    return 0;
}

static int
test_recursive_removal(void)
{
    struct rm_options opts;
    struct rm_walk_state state;
    volatile sig_atomic_t interrupted;

    mkdir("tree", 0755);
    mkdir("tree/sub", 0755);
    CHECK(create_file("tree/sub/file") == 0);
    mkdir("outside", 0755);
    CHECK(create_file("outside/keep") == 0);
    CHECK(symlink("../outside", "tree/linkdir") == 0);

    rm_options_init(&opts, "rm");
    opts.recursive = true;
    opts.preserve_root = true;
    interrupted = 0;
    state.opts = &opts;
    state.prompt_input = NULL;
    state.interrupted = &interrupted;

    CHECK(rm_remove_operand(&state, "tree") == RM_WALK_REMOVED);
    CHECK(access("tree", F_OK) != 0);
    CHECK(access("outside/keep", F_OK) == 0);
    return 0;
}

static int
test_dir_mode_removal(void)
{
    struct rm_options opts;
    struct rm_walk_state state;
    volatile sig_atomic_t interrupted;

    mkdir("emptydir", 0755);

    rm_options_init(&opts, "rm");
    opts.dir_mode = true;
    interrupted = 0;
    state.opts = &opts;
    state.prompt_input = NULL;
    state.interrupted = &interrupted;

    CHECK(rm_remove_operand(&state, "emptydir") == RM_WALK_REMOVED);
    CHECK(access("emptydir", F_OK) != 0);
    return 0;
}

int
main(void)
{
    char template[] = "/tmp/rm-walk-XXXXXX";
    char *temporary_directory;
    char *original_cwd;
    int status;

    status = 0;
    temporary_directory = mkdtemp(template);
    CHECK(temporary_directory != NULL);
    original_cwd = getcwd(NULL, 0);
    CHECK(original_cwd != NULL);
    CHECK(chdir(temporary_directory) == 0);

    if (test_recursive_removal() != 0 || test_dir_mode_removal() != 0) {
        status = 1;
    }

    CHECK(chdir(original_cwd) == 0);
    free(original_cwd);
    printf("test_walk: %s\n", status == 0 ? "ok" : "fail");
    return status;
}