/*
 * linkfollow — link(2) links a symlink itself; linkat(2) follows it only
 * with AT_SYMLINK_FOLLOW.
 *
 * kern_link resolved the old path with vfs_lookup, which follows a final
 * symlink: `ln -P good new` linked the target and `ln -P dangling new`
 * failed with ENOENT.  The BSD linkats dropped their flag and Linux linkat
 * was ENOSYS.  link_fs also handed a node from another filesystem to the
 * directory's link op, which reads it as its own node type.
 *
 * Runs in a scratch directory under argv[1] (default /tmp).  Portable: on
 * Linux link(2) does not follow; on FreeBSD it does, so step B is checked
 * only through linkat there.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static ino_t lino(const char *p)
{
    struct stat st;
    return lstat(p, &st) == 0 ? st.st_ino : 0;
}

static int is_link(const char *p)
{
    struct stat st;
    return lstat(p, &st) == 0 && S_ISLNK(st.st_mode);
}

int main(int argc, char **argv)
{
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/linkfollow.%d", argc > 1 ? argv[1] : "/tmp",
             (int)getpid());
    if (mkdir(dir, 0700) != 0 || chdir(dir) != 0) {
        perror(dir);
        return 2;
    }

    int fd = open("f", O_WRONLY | O_CREAT, 0600);
    check(fd >= 0 && write(fd, "x", 1) == 1, "create f");
    close(fd);
    check(symlink("f", "s") == 0, "symlink s -> f");
    check(symlink("nowhere", "d") == 0, "symlink d -> nowhere");
    mkdir("sub", 0700);

#ifndef __FreeBSD__
    /* A: link(2) links the symlink itself. */
    check(link("s", "h1") == 0 && is_link("h1") && lino("h1") == lino("s"),
          "link(s) links the symlink");
    errno = 0;
    check(link("d", "h2") == 0 && is_link("h2"), "link(dangling) works");
#endif

    /* B: linkat without the flag does not follow; with it, it does. */
    check(linkat(AT_FDCWD, "s", AT_FDCWD, "h3", 0) == 0 && is_link("h3"),
          "linkat(s, 0) links the symlink");
    check(linkat(AT_FDCWD, "s", AT_FDCWD, "h4", AT_SYMLINK_FOLLOW) == 0 &&
          !is_link("h4") && lino("h4") == lino("f"),
          "linkat(s, AT_SYMLINK_FOLLOW) links the target");
    errno = 0;
    check(linkat(AT_FDCWD, "d", AT_FDCWD, "h5", AT_SYMLINK_FOLLOW) == -1 &&
          errno == ENOENT, "linkat(dangling, AT_SYMLINK_FOLLOW) is ENOENT");

    /* C: directory file descriptors. */
    int dfd = open("sub", O_RDONLY | O_DIRECTORY);
    check(dfd >= 0, "open sub");
    check(linkat(AT_FDCWD, "f", dfd, "g", 0) == 0 && lino("sub/g") == lino("f"),
          "linkat into a dirfd");
    check(linkat(dfd, "g", dfd, "g2", 0) == 0 && lino("sub/g2") == lino("f"),
          "linkat between dirfds");
    close(dfd);

    /* D: never a directory, never across filesystems. */
    errno = 0;
    check(link("sub", "h6") == -1 && errno == EPERM, "link(dir) is EPERM");
    errno = 0;
    check(link("/dev/null", "h7") == -1 && errno == EXDEV,
          "link across filesystems is EXDEV");

    const char *names[] = { "sub/g2", "sub/g", "h7", "h6", "h5", "h4", "h3",
                            "h2", "h1", "d", "s", "f", NULL };
    for (int i = 0; names[i]; i++)
        unlink(names[i]);
    rmdir("sub");
    chdir("/");
    rmdir(dir);

    printf("linkfollow: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
