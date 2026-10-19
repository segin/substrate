/*
 * unlinkread — files read, unlinked while open and then closed give
 * back all their memory.
 *
 * ext2 finishes deleting such a file at its last close and clears the
 * node slot's filesystem pointer; the slot's four read scratch buffers
 * (one block each) were freed on recycling only while that pointer was
 * set, so every read+unlink-while-open file leaked 4 KiB.  Compilers'
 * temporary files are used exactly this way.
 *
 * And the block cache gave every new block a new header while empty ones
 * (from freed blocks) sat unused, so deleting files piled up empty headers
 * toward the cache's ceiling (~15 MB at 512 MiB).
 *
 * 100 warm-up rounds, then 2000: create a file in /tmp, write 8 KiB, read
 * it back, unlink it while open, close it.  MemAvailable (free memory plus
 * the reclaimable caches) must not drop by more than 2 MiB over the 2000;
 * the two leaks lost ~25 MB.  Guest only: a busy host is too noisy.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define WARMUP   100
#define ROUNDS   2000
#define SLACK_KB 2048
#define PATH     "/tmp/unlinkread.tmp"

/* MemAvailable: the block cache holding the new files' blocks is
 * reclaimable, not lost. */
static long mem_free_kb(void)
{
    char line[128];
    long kb = -1;
    FILE *f = fopen("/proc/meminfo", "r");

    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "MemAvailable: %ld", &kb) == 1)
            break;
    fclose(f);
    return kb;
}

static int round_trip(void)
{
    static char out[8192], in[8192];
    int fd = open(PATH, O_RDWR | O_CREAT | O_TRUNC, 0600);

    if (fd < 0)
        return -1;
    memset(out, 'u', sizeof(out));
    if (write(fd, out, sizeof(out)) != (ssize_t)sizeof(out) ||
        pread(fd, in, sizeof(in), 0) != (ssize_t)sizeof(in) ||
        unlink(PATH) != 0) {
        close(fd);
        return -1;
    }
    close(fd);          /* the deferred delete runs here */
    return memcmp(in, out, sizeof(in)) == 0 ? 0 : -1;
}

int main(void)
{
    long before, after;
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < WARMUP; i++)
        if (round_trip() != 0)
            bad++;
    before = mem_free_kb();
    for (i = 0; i < ROUNDS; i++)
        if (round_trip() != 0)
            bad++;
    after = mem_free_kb();

    printf("%s %d rounds ran %s\n", bad ? "FAIL " : "ok   ",
           WARMUP + ROUNDS, bad ? "with failures" : "cleanly");
    printf("      MemAvailable %ld -> %ld kB over %d read+unlink+close\n",
           before, after, ROUNDS);
    int leak = before >= 0 && after >= 0 && before - after > SLACK_KB;
    printf("%s memory comes back (within %d kB)\n", leak ? "FAIL " : "ok   ",
           SLACK_KB);
    printf("%s\n", bad || leak ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return bad || leak ? 1 : 0;
}
