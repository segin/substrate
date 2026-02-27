#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct corpus_file {
    char path[PATH_MAX];
};

static uint32_t
xorshift32(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int
load_corpus(const char *dir_path, struct corpus_file **out_files, size_t *out_count)
{
    DIR *dir;
    struct dirent *de;
    struct corpus_file *files = NULL;
    size_t count = 0;
    size_t cap = 0;

    dir = opendir(dir_path);
    if (dir == NULL) {
        perror("opendir corpus");
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        struct corpus_file *grown;
        int n;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        if (count == cap) {
            size_t new_cap = (cap == 0) ? 16u : cap * 2u;
            grown = (struct corpus_file *)realloc(files,
                new_cap * sizeof(*grown));
            if (grown == NULL) {
                closedir(dir);
                free(files);
                return -1;
            }
            files = grown;
            cap = new_cap;
        }

        n = snprintf(files[count].path, sizeof(files[count].path), "%s/%s",
            dir_path, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(files[count].path)) {
            closedir(dir);
            free(files);
            return -1;
        }

        ++count;
    }

    closedir(dir);

    if (count == 0) {
        fprintf(stderr, "fuzz_traversal_paths: empty corpus at %s\n", dir_path);
        free(files);
        return -1;
    }

    *out_files = files;
    *out_count = count;
    return 0;
}

static size_t
read_seed(const char *path, unsigned char *buf, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t n;

    if (fp == NULL) {
        return 0;
    }
    n = fread(buf, 1, cap, fp);
    fclose(fp);
    return n;
}

static int
spawn_chmod(const char *chmod_bin, const char *flag, const char *mode,
    const char *path)
{
    char *const argv[] = {
        (char *)chmod_bin,
        (char *)"-R",
        (char *)flag,
        (char *)mode,
        (char *)path,
        NULL
    };
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execv(chmod_bin, argv);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static void
make_name(const unsigned char *seed, size_t seed_len, char *out, size_t out_len)
{
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-";
    size_t i;

    if (out_len == 0) {
        return;
    }
    if (seed_len == 0) {
        out[0] = 'x';
        if (out_len > 1) {
            out[1] = '\0';
        }
        return;
    }

    for (i = 0; i + 1 < out_len && i < seed_len; ++i) {
        out[i] = alphabet[seed[i] % (sizeof(alphabet) - 1)];
    }
    out[i] = '\0';

    if (strcmp(out, ".") == 0 || strcmp(out, "..") == 0) {
        out[0] = 'x';
        if (out_len > 1) {
            out[1] = '\0';
        }
    }
}

static int
create_file(const char *path)
{
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    if (write(fd, "x", 1) != 1) {
        close(fd);
        return -1;
    }
    return close(fd);
}

static int
setup_tree(const char *tmp_dir, const unsigned char *seed, size_t seed_len,
    char *root_out, size_t root_out_len)
{
    char root[PATH_MAX];
    char outside[PATH_MAX];
    char file_in[PATH_MAX];
    char file_out[PATH_MAX];
    char symlink_path[PATH_MAX];
    char weird_name[64];

    if (snprintf(root, sizeof(root), "%s/root", tmp_dir) >= (int)sizeof(root)) {
        return -1;
    }
    if (snprintf(outside, sizeof(outside), "%s/outside", tmp_dir) >=
        (int)sizeof(outside)) {
        return -1;
    }

    if (mkdir(root, 0755) != 0 || mkdir(outside, 0755) != 0) {
        return -1;
    }

    if (snprintf(file_in, sizeof(file_in), "%s/infile", root) >=
        (int)sizeof(file_in)) {
        return -1;
    }
    if (snprintf(file_out, sizeof(file_out), "%s/outfile", outside) >=
        (int)sizeof(file_out)) {
        return -1;
    }

    if (create_file(file_in) != 0 || create_file(file_out) != 0) {
        return -1;
    }

    if (snprintf(symlink_path, sizeof(symlink_path), "%s/link_out", root) >=
        (int)sizeof(symlink_path)) {
        return -1;
    }
    if (symlink("../outside/outfile", symlink_path) != 0) {
        return -1;
    }

    make_name(seed, seed_len, weird_name, sizeof(weird_name));
    if (snprintf(file_in, sizeof(file_in), "%s/%s", root, weird_name) >=
        (int)sizeof(file_in)) {
        return -1;
    }
    (void)create_file(file_in);

    if (snprintf(root_out, root_out_len, "%s", root) >= (int)root_out_len) {
        return -1;
    }

    return 0;
}

int
main(int argc, char *argv[])
{
    struct corpus_file *files = NULL;
    size_t file_count = 0;
    size_t iterations;
    const char *chmod_bin;
    unsigned char seed_buf[256];
    uint32_t rng = 0x1234ABCDu;
    size_t i;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <corpus_dir> [iterations]\n", argv[0]);
        return 1;
    }

    chmod_bin = getenv("CHMOD_BIN");
    if (chmod_bin == NULL || chmod_bin[0] == '\0') {
        chmod_bin = "../../bin/chmod/chmod";
    }

    iterations = (argc == 3) ? (size_t)strtoul(argv[2], NULL, 10) : 100u;

    if (load_corpus(argv[1], &files, &file_count) != 0) {
        return 1;
    }

    for (i = 0; i < iterations; ++i) {
        char tmp_template[] = "/tmp/chmod-fuzz-path-XXXXXX";
        char *tmp_dir;
        char root[PATH_MAX];
        const char *policy;
        const char *mode;
        int rc;
        size_t idx = xorshift32(&rng) % file_count;
        size_t n = read_seed(files[idx].path, seed_buf, sizeof(seed_buf));

        tmp_dir = mkdtemp(tmp_template);
        if (tmp_dir == NULL) {
            perror("mkdtemp");
            free(files);
            return 1;
        }

        if (setup_tree(tmp_dir, seed_buf, n, root, sizeof(root)) != 0) {
            fprintf(stderr, "fuzz_traversal_paths: setup failed\n");
            free(files);
            return 1;
        }

        switch (xorshift32(&rng) % 3u) {
        case 0:
            policy = "-P";
            break;
        case 1:
            policy = "-H";
            break;
        default:
            policy = "-L";
            break;
        }

        mode = (xorshift32(&rng) & 1u) ? "700" : "u=rwX,g+rX,o-rwx";
        rc = spawn_chmod(chmod_bin, policy, mode, root);
        if (rc < 0 || rc > 1) {
            fprintf(stderr,
                "fuzz_traversal_paths: unexpected chmod status=%d policy=%s\n",
                rc, policy);
            free(files);
            return 1;
        }

        {
            char cmd[PATH_MAX + 32];
            if (snprintf(cmd, sizeof(cmd), "rm -rf %s", tmp_dir) <
                (int)sizeof(cmd)) {
                (void)system(cmd);
            }
        }
    }

    free(files);
    puts("fuzz_traversal_paths: ok");
    return 0;
}
