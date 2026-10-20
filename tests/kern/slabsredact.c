/*
 * slabsredact — sys_vm_slabs shows kernel addresses to root only.
 *
 * Every record named its allocation site as "pc=0x<kernel text address>",
 * and any user could read them, which gives away where the kernel sits.
 * Run as root: root still sees the addresses; a child at uid 1000 sees
 * the same number of records and no address.  Substrate only.
 */
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXREC 512

static sys_slabinfo_t recs[MAXREC];

/* Records read, and how many carry "pc=". */
static int scan(int *with_pc)
{
    size_t n = MAXREC;
    *with_pc = 0;
    if (sys_vm_slabs(recs, &n) != 0)
        return -1;
    for (size_t i = 0; i < n; i++)
        if (strncmp(recs[i].name, "pc=", 3) == 0)
            (*with_pc)++;
    return (int)n;
}

int main(void)
{
    int fails = 0, pc = 0;
    int n = scan(&pc);
    printf("%s root: %d records, %d with an address\n",
           n > 0 && pc == n ? "ok   " : "FAIL ", n, pc);
    if (!(n > 0 && pc == n)) fails++;

    pid_t c = fork();
    if (c == 0) {
        int upc = 0;
        if (setuid(1000) != 0) _exit(255);
        int un = scan(&upc);
        _exit(un > 0 && upc == 0 ? 0 : 1);
    }
    int st = 0;
    waitpid(c, &st, 0);
    int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    printf("%s uid 1000: records without addresses\n", ok ? "ok   " : "FAIL ");
    if (!ok) fails++;

    printf("slabsredact: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
