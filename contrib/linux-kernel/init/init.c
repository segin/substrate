/*
 * init.c -- the userspace init for booting the substrate-built Linux:
 * a static i386 Linux program with no C library, built by substrate's
 * own cc and ld (contrib/linux-kernel/build.sh).  It says it ran, with
 * what uname(2) reports, on the console, then powers the machine off, so
 * a qemu boot test ends on its own.
 */

#define SYS_exit      1
#define SYS_write     4
#define SYS_reboot    88
#define SYS_uname     122

#define LINUX_REBOOT_MAGIC1      0xfee1dead
#define LINUX_REBOOT_MAGIC2      672274793
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedc

struct new_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static long sys3(long nr, long a, long b, long c) {
    long r;

    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

static long sys4(long nr, long a, long b, long c, long d) {
    long r;

    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
    return r;
}

static unsigned long slen(const char *s) {
    unsigned long n = 0;

    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void put(const char *s) {
    sys3(SYS_write, 1, (long)s, (long)slen(s));
}

void _start(void) {
    static struct new_utsname u;

    put("\nSUBSTRATE-LINUX-INIT: userspace reached\n");
    if (sys3(SYS_uname, (long)&u, 0, 0) == 0) {
        put("SUBSTRATE-LINUX-INIT: ");
        put(u.sysname);
        put(" ");
        put(u.release);
        put(" ");
        put(u.machine);
        put("\n");
    }
    put("SUBSTRATE-LINUX-INIT: PASS\n");
    sys4(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_POWER_OFF, 0);
    for (;;) {
        sys3(SYS_exit, 0, 0, 0);
    }
}
