#include "cp_atomic.h"
#include "cp_path.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    return 1; \
} } while (0)

int main(void)
{
    const char *tmpbase = getenv("TMPDIR");
    const char suffix[] = "/cp_atomic_test.XXXXXX";
    char *tmpdir;
    char *dir;
    char *dest;
    char *tmp_path = NULL;
    int fd = -1;
    int in;
    char buf[16];

    if (!tmpbase || !*tmpbase) {
        tmpbase = "/tmp";
    }

    tmpdir = (char *)malloc(strlen(tmpbase) + sizeof(suffix));
    CHECK(tmpdir != NULL);
    snprintf(tmpdir, strlen(tmpbase) + sizeof(suffix), "%s%s", tmpbase, suffix);

    dir = mkdtemp(tmpdir);
    CHECK(dir != NULL);

    dest = cp_path_join(dir, "dest.txt");
    CHECK(dest != NULL);

    CHECK(cp_atomic_open_temp(dest, 0600, &tmp_path, &fd) == 0);
    CHECK(fd >= 0);
    CHECK(write(fd, "hello", 5) == 5);
    CHECK(cp_atomic_commit(fd, tmp_path, dest) == 0);
    fd = -1;

    in = open(dest, O_RDONLY, 0);
    CHECK(in >= 0);
    CHECK(read(in, buf, sizeof(buf)) == 5);
    CHECK(memcmp(buf, "hello", 5) == 0);
    close(in);

    unlink(dest);
    rmdir(dir);
    free(dest);
    free(tmp_path);
    free(tmpdir);
    return 0;
}
