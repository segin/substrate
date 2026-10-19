/*
 * chownperm — chown follows POSIX's permission rules and reaches the disk.
 *
 * The chown family let a file's owner set its group to any group at all
 * (0 included), and wrote the new owner and group only into the in-memory
 * node, so ext2 never recorded them.
 *
 * As root, makes /tmp/chownperm.f (owner 1000:1000, mode 06755), then
 * drops to uid 1000 with groups 1000 and 2000:
 *   - chown(f, -1, 0) and chown(f, 0, -1)  -> EPERM;
 *   - chown(f, -1, 2000)                   -> 0, and clears 06000;
 *   - another uid (1001) may change nothing -> EPERM.
 * It leaves the file in place: the harness then reads its inode from the
 * disk image (gid 2000, mode 0755) to check the change was written.
 *
 * Needs root.  Runs on the host too (sudo; uses /tmp).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define F "/tmp/chownperm.f"

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static int eperm(int r)
{
    return r == -1 && errno == EPERM;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (geteuid() != 0) {
        printf("chownperm: SKIP (needs root)\n");
        return 0;
    }
    unlink(F);
    int fd = open(F, O_CREAT | O_WRONLY, 0644);
    check(fd >= 0, "create");
    if (fd < 0) {
        printf("chownperm: FAIL\n");
        return 1;
    }
    close(fd);
    check(chown(F, 1000, 1000) == 0 && chmod(F, 06755) == 0,
          "root sets 1000:1000, mode 06755");

    pid_t pid = fork();
    if (pid == 0) {
        gid_t groups[] = { 1000, 2000 };
        if (setgroups(2, groups) || setresgid(1000, 1000, 1000) ||
            setresuid(1000, 1000, 1000))
            _exit(0x40);
        int bad = 0;
        bad |= !eperm(chown(F, -1, 0)) << 0;      /* not a member */
        bad |= !eperm(chown(F, 0, -1)) << 1;      /* give it away */
        bad |= (chown(F, -1, 2000) != 0) << 2;    /* supplementary group */
        struct stat st;
        bad |= (stat(F, &st) != 0 || st.st_gid != 2000 ||
                (st.st_mode & 07777) != 0755) << 3;
        _exit(bad);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    int bad = WIFEXITED(st) ? WEXITSTATUS(st) : 0xff;
    printf("      owner result bits %#x\n", bad);
    check(!(bad & 1), "owner cannot set a group it is not in");
    check(!(bad & 2), "owner cannot give the file away");
    check(!(bad & 4), "owner can set its supplementary group");
    check(!(bad & 8), "after it: gid 2000, setuid/setgid cleared");

    pid = fork();
    if (pid == 0) {
        if (setresgid(1001, 1001, 1001) || setresuid(1001, 1001, 1001))
            _exit(0x40);
        _exit(eperm(chown(F, -1, 1001)) ? 0 : 1);
    }
    st = 0;
    waitpid(pid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "a non-owner can change nothing");

    sync();
    printf("chownperm: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
