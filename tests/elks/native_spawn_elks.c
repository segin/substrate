typedef unsigned int uint32_t;
typedef int int32_t;

#define SYS_EXIT    1
#define SYS_FORK    2
#define SYS_WRITE   4
#define SYS_WAITPID 7
#define SYS_EXECVE  11

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
        "push $0\n\t"
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

static int strlen(const char *s) {
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void write_str(const char *s) {
    syscall3(SYS_WRITE, 1, (uint32_t)s, (uint32_t)strlen(s));
}

static void write_num(unsigned int value) {
    char buf[16];
    int i = 0;

    if (value == 0) {
        write_str("0");
        return;
    }

    while (value > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        char ch = buf[--i];
        syscall3(SYS_WRITE, 1, (uint32_t)&ch, 1);
    }
}

static void sys_exit(int code) {
    syscall1(SYS_EXIT, (uint32_t)code);
    __builtin_unreachable();
}

static int sys_fork(void) {
    return syscall0(SYS_FORK);
}

static int sys_waitpid(int pid, int *status, int options) {
    return syscall3(SYS_WAITPID, (uint32_t)pid, (uint32_t)status,
                    (uint32_t)options);
}

static int sys_execve(const char *path, char *const argv[], char *const envp[]) {
    return syscall3(SYS_EXECVE, (uint32_t)path, (uint32_t)argv,
                    (uint32_t)envp);
}

int main(void);

void _start(void) {
    sys_exit(main());
}

int main(void) {
    static const char path[] = "/perso/elks/bin/ls";
    static char *const argv[] = { (char *)path, 0 };
    static char *const envp[] = { 0 };
    int status = 0;
    int pid;

    pid = sys_fork();
    if (pid < 0) {
        write_str("native spawn: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        int rc = sys_execve(path, argv, envp);
        write_str("native spawn: execve failed ");
        write_num((unsigned int)(-rc));
        write_str("\n");
        sys_exit(127);
    }

    if (sys_waitpid(pid, &status, 0) < 0) {
        write_str("native spawn: waitpid failed\n");
        return 1;
    }

    if ((status & 0x7f) == 0) {
        write_str("native spawn: /perso/elks/bin/ls exit ");
        write_num((unsigned int)((status >> 8) & 0xff));
        write_str("\n");
        return ((status >> 8) & 0xff) == 0 ? 0 : 1;
    }

    write_str("native spawn: /perso/elks/bin/ls signal ");
    write_num((unsigned int)(status & 0x7f));
    write_str("\n");
    return 1;
}
