/*
 * Native Substrate personality test program (standalone)
 * Lists / and /bin directories (long format), shows utsname, exits
 * 
 * Build: gcc -m32 -nostdlib -fno-pie -o native_test_standalone native_test_standalone.c
 * 
 * Uses raw syscalls for native (substrate) personality.
 * Native personality uses FreeBSD-style stack-based syscall ABI.
 * Properly detects and displays symbolic links with their targets.
 */

typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef long int32_t;
typedef long long int64_t;
typedef int32_t ssize_t;

/* Native Substrate syscall numbers */
#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_STAT    106
#define SYS_LSTAT   107
#define SYS_READLINK 85   /* Native readlink syscall */
#define SYS_GETDENTS 141
#define SYS_FORK    2
#define SYS_GETPID  20
#define SYS_WAITPID 7
#define SYS_UNAME   122

/* Open flags */
#define O_RDONLY    0

/* File type constants */
#define S_IFMT   0170000  /* Type of file mask */
#define S_IFLNK  0120000  /* Symbolic link */
#define S_IFDIR  0040000  /* Directory */
#define S_IFREG  0100000  /* Regular file */
#define S_IFCHR  0020000  /* Character device */
#define S_IFBLK  0060000  /* Block device */
#define S_IFIFO  0010000  /* FIFO */
#define S_IFSOCK 0140000  /* Socket */

/* stat structure (simplified, matching kernel) */
struct stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint32_t st_rdev;
    int64_t  st_size;
    uint32_t st_blksize;
    uint32_t st_pad1;
    int64_t  st_blocks;
    int64_t  st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_pad2;
    int64_t  st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_pad3;
    int64_t  st_ctime;
    uint32_t st_ctime_nsec;
    uint32_t st_pad4;
};

/* Directory entry - Linux/BSD compatible */
struct dirent {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    char           d_name[];
};

/* utsname structure */
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

/* Syscall wrapper - Native personality uses stack args */
static inline int32_t syscall0(int num) {
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int32_t syscall1(int num, uint32_t a1) {
    int32_t ret;
    __asm__ volatile (
        "push %2\n\t"
        "push $0\n\t"    /* fake return address */
        "int $0x80\n\t"
        "add $8, %%esp"
        : "=a"(ret)
        : "a"(num), "r"(a1)
        : "memory"
    );
    return ret;
}

static inline int32_t syscall2(int num, uint32_t a1, uint32_t a2) {
    int32_t ret;
    __asm__ volatile (
        "push %3\n\t"
        "push %2\n\t"
        "push $0\n\t"
        "int $0x80\n\t"
        "add $12, %%esp"
        : "=a"(ret)
        : "a"(num), "r"(a1), "r"(a2)
        : "memory"
    );
    return ret;
}

static inline int32_t syscall3(int num, uint32_t a1, uint32_t a2, uint32_t a3) {
    int32_t ret;
    __asm__ volatile (
        "push %4\n\t"
        "push %3\n\t"
        "push %2\n\t"
        "push $0\n\t"
        "int $0x80\n\t"
        "add $16, %%esp"
        : "=a"(ret)
        : "a"(num), "r"(a1), "r"(a2), "r"(a3)
        : "memory"
    );
    return ret;
}

/* Simple syscall wrappers */
static void sys_exit(int code) {
    syscall1(SYS_EXIT, code);
    __builtin_unreachable();
}

static int sys_write(int fd, const char *buf, int len) {
    return syscall3(SYS_WRITE, fd, (uint32_t)buf, len);
}

static int sys_read(int fd, void *buf, int len) {
    return syscall3(SYS_READ, fd, (uint32_t)buf, len);
}

static int sys_open(const char *path, int flags, int mode) {
    return syscall3(SYS_OPEN, (uint32_t)path, flags, mode);
}

static int sys_close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

static int sys_getdents(int fd, void *buf, int count) {
    return syscall3(SYS_GETDENTS, fd, (uint32_t)buf, count);
}

static int sys_stat(const char *path, struct stat *buf) {
    return syscall2(SYS_STAT, (uint32_t)path, (uint32_t)buf);
}

static int sys_lstat(const char *path, struct stat *buf) {
    return syscall2(SYS_LSTAT, (uint32_t)path, (uint32_t)buf);
}

static ssize_t sys_readlink(const char *path, char *buf, int bufsiz) {
    return syscall3(SYS_READLINK, (uint32_t)path, (uint32_t)buf, bufsiz);
}

static int sys_uname(struct utsname *buf) {
    return syscall1(SYS_UNAME, (uint32_t)buf);
}

static int sys_fork(void) {
    return syscall0(SYS_FORK);
}

static int sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

static int sys_waitpid(int pid, int *status, int options) {
    return syscall3(SYS_WAITPID, pid, (uint32_t)status, options);
}

/* String length */
static int strlen(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

/* Print string to stdout */
static void print(const char *s) {
    sys_write(1, s, strlen(s));
}

/* Print a number in decimal */
static void print_num(unsigned long n) {
    char buf[16];
    char *p = buf + sizeof(buf) - 1;
    
    *p = '\0';
    if (n == 0) *--p = '0';
    while (n > 0) {
        *--p = '0' + (n % 10);
        n /= 10;
    }
    print(p);
}

/* Print number right-aligned in field */
static void print_num_pad(unsigned long n, int width) {
    char buf[16];
    char *p = buf + sizeof(buf) - 1;
    int len = 0;
    
    *p = '\0';
    if (n == 0) { *--p = '0'; len = 1; }
    while (n > 0) {
        *--p = '0' + (n % 10);
        n /= 10;
        len++;
    }
    while (len < width) { print(" "); len++; }
    print(p);
}

/* String copy */
static char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static unsigned int dev_major(uint32_t dev) {
    return (unsigned int)((dev >> 8) & 0xffu);
}

static unsigned int dev_minor(uint32_t dev) {
    return (unsigned int)(dev & 0xffu);
}

/* Format mode bits as ls-style string */
static void format_mode(uint16_t mode, char *buf) {
    /* Determine file type using S_IFMT mask */
    switch (mode & S_IFMT) {
        case S_IFDIR:  buf[0] = 'd'; break;
        case S_IFLNK:  buf[0] = 'l'; break;
        case S_IFCHR:  buf[0] = 'c'; break;
        case S_IFBLK:  buf[0] = 'b'; break;
        case S_IFREG:  buf[0] = '-'; break;
        case S_IFIFO:  buf[0] = 'p'; break;
        case S_IFSOCK: buf[0] = 's'; break;
        default:       buf[0] = '?'; break;
    }
    buf[1] = (mode & 0400) ? 'r' : '-';
    buf[2] = (mode & 0200) ? 'w' : '-';
    buf[3] = (mode & 0100) ? 'x' : '-';
    buf[4] = (mode & 0040) ? 'r' : '-';
    buf[5] = (mode & 0020) ? 'w' : '-';
    buf[6] = (mode & 0010) ? 'x' : '-';
    buf[7] = (mode & 0004) ? 'r' : '-';
    buf[8] = (mode & 0002) ? 'w' : '-';
    buf[9] = (mode & 0001) ? 'x' : '-';
    buf[10] = '\0';
}

/* List directory contents with long format and symlink targets */
static void list_dir(const char *path) {
    int fd = sys_open(path, O_RDONLY, 0);
    if (fd < 0) {
        print("  [Cannot open directory]\n");
        return;
    }
    
    char buf[512];
    int nread;
    int pathlen = strlen(path);
    
    while ((nread = sys_getdents(fd, buf, sizeof(buf))) > 0) {
        struct dirent *d = (struct dirent *)buf;
        while ((char *)d < buf + nread) {
            char fullpath[256];
            struct stat st;
            char mode_str[12];
            char link_target[256];
            
            /* Build full path */
            char *p = fullpath;
            const char *s = path;
            while (*s) *p++ = *s++;
            /* Add slash if needed */
            if (pathlen > 0 && path[pathlen - 1] != '/') {
                *p++ = '/';
            }
            s = d->d_name;
            while (*s) *p++ = *s++;
            *p = '\0';
            
            print("  ");
            /* Use lstat to detect symlinks (doesn't follow them) */
            if (sys_lstat(fullpath, &st) == 0) {
                format_mode(st.st_mode, mode_str);
                print(mode_str);
                print(" ");
                if ((st.st_mode & S_IFMT) == S_IFCHR || (st.st_mode & S_IFMT) == S_IFBLK) {
                    print_num_pad(dev_major(st.st_rdev), 3);
                    print(",");
                    print_num_pad(dev_minor(st.st_rdev), 3);
                } else {
                    print_num_pad((unsigned long)st.st_size, 8);
                }
                print("  ");
                print(d->d_name);
                
                /* Check if it's a symlink and read target */
                if ((st.st_mode & S_IFMT) == S_IFLNK) {
                    ssize_t len = sys_readlink(fullpath, link_target, sizeof(link_target) - 1);
                    if (len > 0) {
                        link_target[len] = '\0';
                        print(" -> ");
                        print(link_target);
                    } else {
                        print(" -> [error]");
                    }
                }
                print("\n");
            } else {
                print("?????????? ????????  ");
                print(d->d_name);
                print("\n");
            }
            
            if (d->d_reclen == 0) break; /* Safety */
            d = (struct dirent *)((char *)d + d->d_reclen);
        }
    }
    
    sys_close(fd);
}

static void read_text_file(const char *path) {
    int fd = sys_open(path, O_RDONLY, 0);
    if (fd < 0) {
        print("  [open failed] ");
        print(path);
        print("\n");
        return;
    }

    print("  ");
    print(path);
    print(":\n");

    char buf[256];
    int nread;
    while ((nread = sys_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < nread; i++) {
            if (buf[i] == '\0') buf[i] = ' ';
        }
        sys_write(1, buf, nread);
    }
    print("\n");
    sys_close(fd);
}

static void inspect_procfs(void) {
    int fd = sys_open("/proc", O_RDONLY, 0);
    if (fd < 0) {
        print("Cannot open /proc\n");
        return;
    }

    print("Reading regular files in /proc:\n");
    char buf[512];
    int nread;
    while ((nread = sys_getdents(fd, buf, sizeof(buf))) > 0) {
        struct dirent *d = (struct dirent *)buf;
        while ((char *)d < buf + nread) {
            if (d->d_reclen == 0) break;

            if (d->d_name[0] == '.' &&
                (d->d_name[1] == '\0' || (d->d_name[1] == '.' && d->d_name[2] == '\0'))) {
                d = (struct dirent *)((char *)d + d->d_reclen);
                continue;
            }

            char fullpath[256];
            char *p = fullpath;
            const char *s = "/proc/";
            while (*s) *p++ = *s++;
            s = d->d_name;
            while (*s) *p++ = *s++;
            *p = '\0';

            struct stat st;
            if (sys_lstat(fullpath, &st) == 0) {
                uint16_t type = st.st_mode & S_IFMT;
                if (type == S_IFREG) {
                    read_text_file(fullpath);
                } else if (type == S_IFLNK && strcmp(d->d_name, "self") == 0) {
                    char target[128];
                    ssize_t len = sys_readlink(fullpath, target, sizeof(target) - 1);
                    print("  /proc/self -> ");
                    if (len > 0) {
                        target[len] = '\0';
                        print(target);
                    } else {
                        print("[readlink failed]");
                    }
                    print("\n");
                }
            }

            d = (struct dirent *)((char *)d + d->d_reclen);
        }
    }
    sys_close(fd);

    print("\nContents of /proc/1:\n");
    list_dir("/proc/1");
    print("\n");
}

/* Entry point */
void _start(void) {
    struct utsname uts;
    
    print("=== Native Substrate Personality Test (standalone) ===\n\n");
    
    /* Show utsname */
    print("System Information:\n");
    if (sys_uname(&uts) == 0) {
        print("  sysname:  "); print(uts.sysname); print("\n");
        print("  nodename: "); print(uts.nodename); print("\n");
        print("  release:  "); print(uts.release); print("\n");
        print("  version:  "); print(uts.version); print("\n");
        print("  machine:  "); print(uts.machine); print("\n");
    } else {
        print("  [uname failed]\n");
    }
    print("\n");
    
    /* List / */
    print("Contents of /:\n");
    list_dir("/");
    print("\n");
    
    /* List /bin */
    print("Contents of /bin:\n");
    list_dir("/bin");
    print("\n");

    /* List /proc */
    print("Contents of /proc:\n");
    list_dir("/proc");
    print("\n");
    
    inspect_procfs();
    
    /* Fork Test */
    print("Testing fork() and getpid():\n");
    print("  Parent: PID="); print_num(sys_getpid()); print(" (Before Fork)\n");
    
    int pid = sys_fork();
    if (pid < 0) {
        print("  [fork failed]\n");
    } else if (pid == 0) {
        /* Child */
        print("  Child:  PID="); print_num(sys_getpid()); 
        print(" (Returned from getpid())\n");
        sys_exit(0);
    } else {
        /* Parent */
        print("  Parent: PID="); print_num(sys_getpid()); 
        print(" (Returned from getpid())\n");
        print("  Parent: Child PID="); print_num(pid); print(" created\n");
        int status;
        sys_waitpid(pid, &status, 0);
        print("  Parent: Child exited\n");
    }
    print("\n");
    
    print("=== Test Complete ===\n");
    sys_exit(0);
}
