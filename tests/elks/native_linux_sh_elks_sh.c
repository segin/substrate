typedef unsigned int uint32_t;
typedef int int32_t;

#define SYS_WRITE   4
#define SYS_EXECVE  11
#define SYS_EXIT    1

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

static void sys_exit(int code) {
    syscall1(SYS_EXIT, (uint32_t)code);
    __builtin_unreachable();
}

void _start(void) {
    static const char path[] = "/bin/sh";
    static const char script[] = "/native_sh_elks_sh.sh";
    static char *const argv[] = { (char *)path, (char *)script, 0 };
    static char *const envp[] = { 0 };
    int rc = syscall3(SYS_EXECVE, (uint32_t)path, (uint32_t)argv,
                      (uint32_t)envp);

    write_str("native linux sh: execve failed\n");
    sys_exit(rc < 0 ? (unsigned int)(-rc) : 127);
}
