/*
 * substrate-run - Linux-host runner for Substrate i386 ELF binaries.
 *
 * The runner executes a patched anonymous copy of the ELF image so Linux can
 * load it normally, then uses PTRACE_SYSEMU to intercept Substrate int 0x80
 * syscalls before the Linux i386 syscall ABI sees the wrong argument registers.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef PTRACE_SYSEMU
#define PTRACE_SYSEMU 31
#endif

#ifndef PTRACE_O_EXITKILL
#define PTRACE_O_EXITKILL 0x00100000
#endif

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif

#ifndef __NR_execveat
#define __NR_execveat 358
#endif

#ifndef __NR_mmap2
#define __NR_mmap2 192
#endif

#ifndef __NR_poll
#define __NR_poll 168
#endif

#ifndef __NR_memfd_create
#define __NR_memfd_create 356
#endif

#define LINUX32_NR_MMAP2 192
#define LINUX32_NR_POLL 168
#define LINUX32_NR_EXECVEAT 358
#define LINUX32_NR_SET_THREAD_AREA 243
#define LINUX32_NR_SETPGID 57
#define LINUX32_NR_SETSID 66
#define LINUX32_NR_GETRUSAGE 77
#define LINUX32_NR_GETPGID 132
#define LINUX32_NR_GETSID 147

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_TRACEES 256
#define MAX_GUEST_STRING 4096
#define SCRATCH_SIZE 4096U

#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ELFOSABI_SYSV 0
#define ELFOSABI_SUBSTRATE 64
#define ET_EXEC 2
#define ET_DYN 3
#define EM_386 3

#define SUB_SYS_EXIT 1
#define SUB_SYS_FORK 2
#define SUB_SYS_READ 3
#define SUB_SYS_WRITE 4
#define SUB_SYS_OPEN 5
#define SUB_SYS_CLOSE 6
#define SUB_SYS_WAITPID 7
#define SUB_SYS_CREAT 8
#define SUB_SYS_LINK 9
#define SUB_SYS_UNLINK 10
#define SUB_SYS_EXECVE 11
#define SUB_SYS_CHDIR 12
#define SUB_SYS_TIME 13
#define SUB_SYS_MKNOD 14
#define SUB_SYS_CHMOD 15
#define SUB_SYS_LCHOWN 16
#define SUB_SYS_LSEEK 19
#define SUB_SYS_GETPID 20
#define SUB_SYS_MOUNT 21
#define SUB_SYS_UMOUNT 22
#define SUB_SYS_SETUID 23
#define SUB_SYS_GETUID 24
#define SUB_SYS_STIME 25
#define SUB_SYS_PTRACE 26
#define SUB_SYS_ALARM 27
#define SUB_SYS_ACCESS 33
#define SUB_SYS_SYNC 36
#define SUB_SYS_KILL 37
#define SUB_SYS_RENAME 38
#define SUB_SYS_MKDIR 39
#define SUB_SYS_RMDIR 40
#define SUB_SYS_DUP 41
#define SUB_SYS_PIPE 42
#define SUB_SYS_TIMES 43
#define SUB_SYS_BRK 45
#define SUB_SYS_SETGID 46
#define SUB_SYS_GETGID 47
#define SUB_SYS_SIGNAL 48
#define SUB_SYS_GETEUID 49
#define SUB_SYS_GETEGID 50
#define SUB_SYS_ACCT 51
#define SUB_SYS_IOCTL 54
#define SUB_SYS_FCNTL 55
#define SUB_SYS_UMASK 60
#define SUB_SYS_CHROOT 61
#define SUB_SYS_DUP2 63
#define SUB_SYS_GETPPID 64
#define SUB_SYS_SIGACTION 67
#define SUB_SYS_SIGPROCMASK 126
#define SUB_SYS_GETGROUPS 80
#define SUB_SYS_SETGROUPS 81
#define SUB_SYS_SYMLINK 83
#define SUB_SYS_READLINK 85
#define SUB_SYS_REBOOT 88
#define SUB_SYS_MMAP 90
#define SUB_SYS_MUNMAP 91
#define SUB_SYS_TRUNCATE 92
#define SUB_SYS_FTRUNCATE 93
#define SUB_SYS_FCHMOD 94
#define SUB_SYS_FCHOWN 95
#define SUB_SYS_SETPRIORITY 96
#define SUB_SYS_GETPRIORITY 100
#define SUB_SYS_GETRUSAGE 117
#define SUB_SYS_WAIT4 114
#define SUB_SYS_STAT 106
#define SUB_SYS_LSTAT 107
#define SUB_SYS_FSTAT 108
#define SUB_SYS_SIGRETURN 119
#define SUB_SYS_CLONE 120
#define SUB_SYS_UNAME 122
#define SUB_SYS_MODIFY_LDT 123
#define SUB_SYS_GETDENTS 141
#define SUB_SYS_MSYNC 144
#define SUB_SYS_SETSID 147
#define SUB_SYS_MLOCK 150
#define SUB_SYS_MUNLOCK 151
#define SUB_SYS_STATFS 157
#define SUB_SYS_FSTATFS 158
#define SUB_SYS_NANOSLEEP 162
#define SUB_SYS_SETPGID 181
#define SUB_SYS_GETPGID 182
#define SUB_SYS_GETCWD 183
#define SUB_SYS_SIGALTSTACK 186
#define SUB_SYS_SYSCTL 202
#define SUB_SYS_POLL 209
#define SUB_SYS_FUTEX 240
#define SUB_SYS_CLOCK_GETTIME 265
#define SUB_SYS_GETSID 310

#define SUB_TIOCSCTTY 0x540E
#define SUB_TIOCGPGRP 0x540F
#define SUB_TIOCSPGRP 0x5410
#define SUB_TIOCGWINSZ 0x5413
#define SUB_TIOCSWINSZ 0x5414
#define SUB_FIONREAD 0x541B
#define SUB_TIOCNOTTY 0x5422
#define SUB_TCGETS 0x5401
#define SUB_TCSETS 0x5402
#define SUB_TCSETSW 0x5403
#define SUB_TCSETSF 0x5404
#define LINUX32_NCCS 19

#define SUB_SA_NOCLDSTOP 0x00000001U
#define SUB_SA_NOCLDWAIT 0x00000002U
#define SUB_SA_SIGINFO   0x00000004U
#define SUB_SA_ONSTACK   0x00000008U
#define SUB_SA_RESTART   0x00000010U
#define SUB_SA_NODEFER   0x00000020U
#define SUB_SA_RESETHAND 0x00000040U

#define SUB_SIG_BLOCK    1U
#define SUB_SIG_UNBLOCK  2U
#define SUB_SIG_SETMASK  3U

#define LINUX_SA_NOCLDSTOP 0x00000001U
#define LINUX_SA_NOCLDWAIT 0x00000002U
#define LINUX_SA_SIGINFO   0x00000004U
#define LINUX_SA_ONSTACK   0x08000000U
#define LINUX_SA_RESTART   0x10000000U
#define LINUX_SA_NODEFER   0x40000000U
#define LINUX_SA_RESETHAND 0x80000000U

#define LINUX_SIG_BLOCK    0U
#define LINUX_SIG_UNBLOCK  1U
#define LINUX_SIG_SETMASK  2U

enum syscall_action {
    SYSCALL_STOPPED,
    SYSCALL_EXECED,
    SYSCALL_EXITED,
};

struct elf32_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

struct linux32_user_desc {
    uint32_t entry_number;
    uint32_t base_addr;
    uint32_t limit;
    uint32_t flags;
};

struct tls_image {
    uint32_t memsz;
    uint32_t filesz;
    uint32_t align;
    unsigned char *init;
};

struct substrate_stat {
    uint32_t st_dev;
    uint64_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint32_t st_rdev;
    int64_t st_size;
    uint32_t st_blksize;
    uint32_t st_pad1;
    int64_t st_blocks;
    int64_t sub_st_atime;
    uint32_t sub_st_atime_nsec;
    uint32_t st_pad2;
    int64_t sub_st_mtime;
    uint32_t sub_st_mtime_nsec;
    uint32_t st_pad3;
    int64_t sub_st_ctime;
    uint32_t sub_st_ctime_nsec;
    uint32_t st_pad4;
} __attribute__((packed));

struct substrate_timespec {
    int64_t tv_sec;
    int32_t tv_nsec;
} __attribute__((packed));

struct substrate_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_line;
    uint8_t c_cc[32];
    uint8_t _pad[3];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
};

struct linux32_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_line;
    uint8_t c_cc[LINUX32_NCCS];
};

struct substrate_sigaction {
    uint32_t handler;
    uint32_t mask;
    uint32_t flags;
};

struct linux32_old_sigaction {
    uint32_t handler;
    uint32_t mask;
    uint32_t flags;
    uint32_t restorer;
};

struct linux32_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} __attribute__((packed));

struct substrate_utsname {
    char sysname[256];
    char nodename[256];
    char release[256];
    char version[256];
    char machine[256];
    char domainname[256];
};

enum tracee_mode {
    TRACEE_SUBSTRATE = 0,
    TRACEE_LINUX,
};

struct tracee {
    pid_t pid;
    bool live;
    bool ready;
    enum tracee_mode mode;
    pid_t guest_parent;
    pid_t guest_pgid;
    bool group_stopped;
    bool replaying;
    bool pending_status_valid;
    int pending_status;
    bool tls_ready;
    uint16_t tls_selector;
    uint32_t tls_trampoline;
};

struct child_event {
    bool live;
    pid_t parent_pid;
    pid_t pid;
    pid_t pgid;
    int status;
};

struct pending_stop {
    bool live;
    pid_t pid;
    int status;
};

struct runner {
    struct tracee tracees[MAX_TRACEES];
    struct child_event child_events[MAX_TRACEES * 4];
    struct pending_stop pending_stops[MAX_TRACEES];
    pid_t root;
    int live_count;
    int root_status;
    bool root_done;
    bool trace;
    bool trace_ioctl;
};

static int dispatch_tracee_status(struct runner *runner, pid_t pid, int status);

static noreturn void
usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-t] [--ioctl-trace] program [args...]\n", prog);
    exit(2);
}

static int
host_errno(void)
{
    return errno ? errno : EIO;
}

static bool
is_errno_ret(long value)
{
    return value < 0 && value >= -4095;
}

static void
trace_log(const struct runner *runner, pid_t pid, const char *fmt, ...)
{
    va_list ap;

    if (!runner->trace) {
        return;
    }

    fprintf(stderr, "substrate-run[%d]: ", (int)pid);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static const char *
ioctl_name(uint32_t request)
{
    switch (request) {
    case SUB_TCGETS:
        return "TCGETS";
    case SUB_TCSETS:
        return "TCSETS";
    case SUB_TCSETSW:
        return "TCSETSW";
    case SUB_TCSETSF:
        return "TCSETSF";
    case SUB_TIOCGWINSZ:
        return "TIOCGWINSZ";
    case SUB_TIOCSWINSZ:
        return "TIOCSWINSZ";
    case SUB_TIOCGPGRP:
        return "TIOCGPGRP";
    case SUB_TIOCSPGRP:
        return "TIOCSPGRP";
    case SUB_FIONREAD:
        return "FIONREAD";
    case SUB_TIOCNOTTY:
        return "TIOCNOTTY";
    case SUB_TIOCSCTTY:
        return "TIOCSCTTY";
    default:
        return NULL;
    }
}
static struct tracee *
runner_find(struct runner *runner, pid_t pid)
{
    for (size_t i = 0; i < ARRAY_SIZE(runner->tracees); i++) {
        if (runner->tracees[i].live && runner->tracees[i].pid == pid) {
            return &runner->tracees[i];
        }
    }
    return NULL;
}

static struct tracee *
runner_add(struct runner *runner, pid_t pid, bool ready, enum tracee_mode mode)
{
    struct tracee *existing = runner_find(runner, pid);

    if (existing) {
        existing->ready = existing->ready || ready;
        return existing;
    }

    for (size_t i = 0; i < ARRAY_SIZE(runner->tracees); i++) {
        if (!runner->tracees[i].live) {
            runner->tracees[i].pid = pid;
            runner->tracees[i].live = true;
            runner->tracees[i].ready = ready;
            runner->tracees[i].mode = mode;
            runner->live_count++;
            return &runner->tracees[i];
        }
    }

    fprintf(stderr, "substrate-run: too many tracees\n");
    return NULL;
}

static void
runner_remove(struct runner *runner, pid_t pid, int status)
{
    struct tracee *tracee = runner_find(runner, pid);

    if (!tracee) {
        return;
    }

    if (pid == runner->root && !runner->root_done) {
        if (WIFEXITED(status)) {
            runner->root_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            runner->root_status = 128 + WTERMSIG(status);
        }
        runner->root_done = true;
    }

    memset(tracee, 0, sizeof(*tracee));
    runner->live_count--;
}

static bool
child_wait_target_matches(pid_t target, pid_t child_pid, pid_t child_pgid,
                          pid_t caller_pgid)
{
    if (target > 0) {
        return child_pid == target;
    }
    if (target == -1) {
        return true;
    }
    if (target == 0) {
        return child_pgid == caller_pgid;
    }
    return child_pgid == -target;
}

static void
runner_note_child_event(struct runner *runner, pid_t parent_pid, pid_t pid,
                        pid_t pgid, int status)
{
    size_t slot = ARRAY_SIZE(runner->child_events);

    if (parent_pid <= 0) {
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(runner->child_events); i++) {
        if (!runner->child_events[i].live) {
            slot = i;
            break;
        }
    }
    if (slot == ARRAY_SIZE(runner->child_events)) {
        slot = 0;
    }

    runner->child_events[slot].live = true;
    runner->child_events[slot].parent_pid = parent_pid;
    runner->child_events[slot].pid = pid;
    runner->child_events[slot].pgid = pgid;
    runner->child_events[slot].status = status;
}

static void
runner_note_pending_stop(struct runner *runner, pid_t pid, int status)
{
    size_t slot = ARRAY_SIZE(runner->pending_stops);

    for (size_t i = 0; i < ARRAY_SIZE(runner->pending_stops); i++) {
        if (!runner->pending_stops[i].live &&
            slot == ARRAY_SIZE(runner->pending_stops)) {
            slot = i;
        }
    }
    if (slot == ARRAY_SIZE(runner->pending_stops)) {
        slot = 0;
    }

    runner->pending_stops[slot].live = true;
    runner->pending_stops[slot].pid = pid;
    runner->pending_stops[slot].status = status;
}

static bool
runner_consume_pending_stop(struct runner *runner, pid_t pid, int *status)
{
    for (size_t i = 0; i < ARRAY_SIZE(runner->pending_stops); i++) {
        struct pending_stop *pending = &runner->pending_stops[i];

        if (!pending->live || pending->pid != pid) {
            continue;
        }
        *status = pending->status;
        pending->live = false;
        return true;
    }
    return false;
}

static bool
runner_dispatch_pending_stop(struct runner *runner)
{
    for (size_t i = 0; i < ARRAY_SIZE(runner->pending_stops); i++) {
        struct pending_stop pending = runner->pending_stops[i];

        if (!pending.live) {
            continue;
        }
        runner->pending_stops[i].live = false;
        (void)dispatch_tracee_status(runner, pending.pid, pending.status);
        return true;
    }
    return false;
}

static bool
runner_consume_child_event(struct runner *runner, pid_t parent_pid, pid_t target,
                           int options, pid_t caller_pgid, pid_t *out_pid,
                           int *out_status)
{
    for (size_t i = 0; i < ARRAY_SIZE(runner->child_events); i++) {
        struct child_event *event = &runner->child_events[i];

        if (!event->live || event->parent_pid != parent_pid) {
            continue;
        }
        if (!child_wait_target_matches(target, event->pid, event->pgid,
                                       caller_pgid)) {
            continue;
        }
        if (WIFSTOPPED(event->status) && !(options & WUNTRACED)) {
            continue;
        }

        *out_pid = event->pid;
        *out_status = event->status;
        event->live = false;
        return true;
    }
    return false;
}

static bool
runner_has_matching_child(struct runner *runner, pid_t parent_pid, pid_t target,
                          pid_t caller_pgid)
{
    for (size_t i = 0; i < ARRAY_SIZE(runner->child_events); i++) {
        struct child_event *event = &runner->child_events[i];

        if (!event->live || event->parent_pid != parent_pid) {
            continue;
        }
        if (child_wait_target_matches(target, event->pid, event->pgid,
                                      caller_pgid)) {
            return true;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(runner->tracees); i++) {
        struct tracee *child = &runner->tracees[i];

        if (!child->live || child->guest_parent != parent_pid) {
            continue;
        }
        if (child_wait_target_matches(target, child->pid, child->guest_pgid,
                                      caller_pgid)) {
            return true;
        }
    }
    return false;
}

static int
set_ptrace_options(pid_t pid)
{
    long opts = PTRACE_O_TRACESYSGOOD |
                PTRACE_O_TRACEEXEC |
                PTRACE_O_TRACEFORK |
                PTRACE_O_TRACEVFORK |
                PTRACE_O_TRACECLONE |
                PTRACE_O_EXITKILL;

    if (ptrace(PTRACE_SETOPTIONS, pid, 0, opts) < 0) {
        return -host_errno();
    }
    return 0;
}

static int
cont_tracee(pid_t pid, enum __ptrace_request request, int sig)
{
    if (ptrace(request, pid, 0, (void *)(uintptr_t)sig) < 0) {
        return -host_errno();
    }
    return 0;
}

static int get_regs(pid_t pid, struct user_regs_struct *regs);
static int set_regs(pid_t pid, const struct user_regs_struct *regs);
static int write_mem(pid_t pid, uint32_t addr, const void *buf, size_t len);
static uint32_t regs_ip(const struct user_regs_struct *regs);
static void regs_set_ip(struct user_regs_struct *regs, uint32_t ip);
static bool is_syscall_stop(int status);
static bool looks_like_substrate_syscall_stop(pid_t pid);
static bool is_job_control_stop_signal(int sig);
static bool is_ptrace_group_stop(pid_t pid, int sig);
static bool signal_target_matches(pid_t target, pid_t pid, pid_t pgid,
                                  pid_t caller_pgid);
static int check_int80(pid_t pid, uint32_t ip);
static void normalize_sysemu_stop(pid_t pid, struct user_regs_struct *regs);
static int adopt_event_child(struct runner *runner, struct tracee *parent,
                             pid_t child);
static int resume_tracee(struct runner *runner, struct tracee *tracee,
                         enum __ptrace_request request, int sig);
static enum __ptrace_request tracee_resume_request(const struct tracee *tracee);
static void tracee_reset_tls(struct tracee *tracee);
static void trace_stop_state(const struct runner *runner, pid_t pid, int sig);
static enum syscall_action handle_substrate_syscall(struct runner *runner,
                                                    struct tracee *tracee);

static int
dispatch_tracee_status(struct runner *runner, pid_t pid, int status)
{
    struct tracee *tracee;
    unsigned int event;
    int sig;
    bool newly_added = false;

    tracee = runner_find(runner, pid);
    if (!tracee) {
        tracee = runner_add(runner, pid, true, TRACEE_SUBSTRATE);
        if (!tracee) {
            return -ENOMEM;
        }
        (void)set_ptrace_options(pid);
        newly_added = true;
    }

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        if (runner->trace) {
            if (WIFEXITED(status)) {
                trace_log(runner, pid, "exit status=%d", WEXITSTATUS(status));
            } else {
                trace_log(runner, pid, "signaled sig=%d", WTERMSIG(status));
            }
        }
        if (tracee->guest_parent > 0) {
            runner_note_child_event(runner, tracee->guest_parent, pid,
                                    tracee->guest_pgid, status);
        }
        runner_remove(runner, pid, status);
        return 0;
    }

    if (!WIFSTOPPED(status)) {
        return 0;
    }

    event = (unsigned int)status >> 16;
    sig = WSTOPSIG(status);

    if (newly_added && event == 0 && sig == SIGSTOP) {
        tracee->ready = true;
        (void)resume_tracee(runner, tracee, tracee_resume_request(tracee), 0);
        return 0;
    }

    if (event == PTRACE_EVENT_FORK ||
        event == PTRACE_EVENT_VFORK ||
        event == PTRACE_EVENT_CLONE) {
        unsigned long msg = 0;

        if (ptrace(PTRACE_GETEVENTMSG, pid, 0, &msg) == 0 && msg != 0) {
            int ret = adopt_event_child(runner, tracee, (pid_t)msg);

            if (ret < 0 && runner->trace) {
                trace_log(runner, pid, "failed to adopt event child %ld: %d",
                          (long)msg, ret);
            }
        }
        (void)resume_tracee(runner, tracee, tracee_resume_request(tracee), 0);
        return 0;
    }

    if (event == PTRACE_EVENT_EXEC) {
        tracee->ready = true;
        tracee_reset_tls(tracee);
        (void)set_ptrace_options(pid);
        (void)resume_tracee(runner, tracee, tracee_resume_request(tracee), 0);
        return 0;
    }

    if (!tracee->ready) {
        tracee->ready = true;
        (void)resume_tracee(runner, tracee, tracee_resume_request(tracee),
                            sig == SIGTRAP ? 0 : sig);
        return 0;
    }

    if (tracee->mode == TRACEE_SUBSTRATE &&
        is_syscall_stop(status) &&
        looks_like_substrate_syscall_stop(pid)) {
        enum syscall_action action = handle_substrate_syscall(runner, tracee);

        if (action == SYSCALL_STOPPED || action == SYSCALL_EXECED) {
            (void)resume_tracee(runner, tracee, tracee_resume_request(tracee), 0);
        }
        return 0;
    }

    if (sig == SIGTRAP && event == 0) {
        (void)resume_tracee(runner, tracee, tracee_resume_request(tracee), 0);
        return 0;
    }

    if (tracee->guest_parent > 0 && is_job_control_stop_signal(sig)) {
        if (is_ptrace_group_stop(pid, sig)) {
            tracee->group_stopped = true;
            runner_note_child_event(runner, tracee->guest_parent, pid,
                                    tracee->guest_pgid, status);
            if (runner->trace) {
                trace_log(runner, pid, "job-control group-stop sig=%d", sig);
            }
        } else {
            (void)resume_tracee(runner, tracee, tracee_resume_request(tracee), sig);
        }
        return 0;
    }

    if (runner->trace) {
        trace_log(runner, pid, "stop sig=%d event=%u", sig, event);
    }
    if (sig != SIGTRAP) {
        trace_stop_state(runner, pid, sig);
    }
    (void)resume_tracee(runner, tracee, tracee_resume_request(tracee), sig);
    return 0;
}

static int
resume_tracee(struct runner *runner, struct tracee *tracee,
              enum __ptrace_request request, int sig)
{
    struct user_regs_struct regs;
    unsigned char code[13];
    uint32_t ip;
    int32_t rel;
    int ret;

    (void)runner;

    if (!tracee->tls_ready || tracee->tls_selector == 0 ||
        tracee->tls_trampoline == 0 ||
        (request != PTRACE_SYSEMU && request != PTRACE_CONT)) {
        return cont_tracee(tracee->pid, request, sig);
    }

    /*
     * Deliver asynchronous signals with the tracee's current live %gs state
     * instead of redirecting through the TLS trampoline. The trampoline works
     * for ordinary resumes, but signal-delivery stops can land mid-trampoline
     * and leave the tracee executing from scratch space.
     */
    if (sig != 0) {
        return cont_tracee(tracee->pid, request, sig);
    }

    ret = get_regs(tracee->pid, &regs);
    if (ret < 0) {
        return ret;
    }

    ip = regs_ip(&regs);
    rel = (int32_t)(ip - (tracee->tls_trampoline + (uint32_t)sizeof(code)));
    code[0] = 0x50;
    code[1] = 0x66;
    code[2] = 0xb8;
    code[3] = (unsigned char)(tracee->tls_selector & 0xffU);
    code[4] = (unsigned char)(tracee->tls_selector >> 8);
    code[5] = 0x8e;
    code[6] = 0xe8;
    code[7] = 0x58;
    code[8] = 0xe9;
    memcpy(&code[9], &rel, sizeof(rel));

    ret = write_mem(tracee->pid, tracee->tls_trampoline, code, sizeof(code));
    if (ret < 0) {
        return ret;
    }

    regs_set_ip(&regs, tracee->tls_trampoline);
    ret = set_regs(tracee->pid, &regs);
    if (ret < 0) {
        return ret;
    }

    return cont_tracee(tracee->pid, request, sig);
}

static enum __ptrace_request
tracee_resume_request(const struct tracee *tracee)
{
    return tracee->mode == TRACEE_SUBSTRATE ? PTRACE_SYSEMU : PTRACE_CONT;
}

static void
tracee_reset_tls(struct tracee *tracee)
{
    tracee->tls_ready = false;
    tracee->tls_selector = 0;
    tracee->tls_trampoline = 0;
}

static bool
should_fallback_native_exec(int err)
{
    switch (-err) {
    case ENOENT:
    case ENOTDIR:
    case EACCES:
    case ENOEXEC:
        return true;
    default:
        return false;
    }
}

static bool
is_syscall_stop(int status)
{
    int sig;

    if (!WIFSTOPPED(status)) {
        return false;
    }

    sig = WSTOPSIG(status);
    return sig == (SIGTRAP | 0x80) || sig == SIGTRAP;
}

static bool
looks_like_substrate_syscall_stop(pid_t pid)
{
    struct user_regs_struct regs;

    if (get_regs(pid, &regs) < 0) {
        return false;
    }
    normalize_sysemu_stop(pid, &regs);
    return check_int80(pid, regs_ip(&regs)) == 0;
}

static bool
is_job_control_stop_signal(int sig)
{
    switch (sig) {
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
        return true;
    default:
        return false;
    }
}

static bool
is_ptrace_group_stop(pid_t pid, int sig)
{
    siginfo_t info;

    if (!is_job_control_stop_signal(sig)) {
        return false;
    }
    errno = 0;
    if (ptrace(PTRACE_GETSIGINFO, pid, 0, &info) == 0) {
        return false;
    }
    return errno == EINVAL;
}

static bool
signal_target_matches(pid_t target, pid_t pid, pid_t pgid, pid_t caller_pgid)
{
    if (target > 0) {
        return pid == target;
    }
    if (target == 0) {
        return pgid == caller_pgid;
    }
    if (target == -1) {
        return true;
    }
    return pgid == -target;
}

static int
read_mem(pid_t pid, uint32_t addr, void *buf, size_t len)
{
    struct iovec local = { .iov_base = buf, .iov_len = len };
    struct iovec remote = {
        .iov_base = (void *)(uintptr_t)addr,
        .iov_len = len,
    };
    size_t done = 0;

    if (len == 0) {
        return 0;
    }

    errno = 0;
    ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (n == (ssize_t)len) {
        return 0;
    }

    while (done < len) {
        uint32_t word_addr = (addr + (uint32_t)done) & ~(uint32_t)(sizeof(long) - 1U);
        size_t offset = (addr + done) - word_addr;
        size_t take = sizeof(long) - offset;
        long word;

        if (take > len - done) {
            take = len - done;
        }

        errno = 0;
        word = ptrace(PTRACE_PEEKDATA, pid, (void *)(uintptr_t)word_addr, 0);
        if (word == -1 && errno != 0) {
            return -host_errno();
        }

        memcpy((char *)buf + done, (const char *)&word + offset, take);
        done += take;
    }

    return 0;
}

static int
write_mem(pid_t pid, uint32_t addr, const void *buf, size_t len)
{
    struct iovec local = { .iov_base = (void *)buf, .iov_len = len };
    struct iovec remote = {
        .iov_base = (void *)(uintptr_t)addr,
        .iov_len = len,
    };
    size_t done = 0;

    if (len == 0) {
        return 0;
    }

    errno = 0;
    ssize_t n = process_vm_writev(pid, &local, 1, &remote, 1, 0);
    if (n == (ssize_t)len) {
        return 0;
    }

    while (done < len) {
        uint32_t word_addr = (addr + (uint32_t)done) & ~(uint32_t)(sizeof(long) - 1U);
        size_t offset = (addr + done) - word_addr;
        size_t take = sizeof(long) - offset;
        long word;

        if (take > len - done) {
            take = len - done;
        }

        errno = 0;
        word = ptrace(PTRACE_PEEKDATA, pid, (void *)(uintptr_t)word_addr, 0);
        if (word == -1 && errno != 0) {
            return -host_errno();
        }

        memcpy((char *)&word + offset, (const char *)buf + done, take);
        if (ptrace(PTRACE_POKEDATA, pid, (void *)(uintptr_t)word_addr,
                   (void *)(uintptr_t)word) < 0) {
            return -host_errno();
        }

        done += take;
    }

    return 0;
}

static int
read_u32(pid_t pid, uint32_t addr, uint32_t *value)
{
    return read_mem(pid, addr, value, sizeof(*value));
}

static int
read_guest_string(pid_t pid, uint32_t addr, char *buf, size_t buflen)
{
    size_t i;

    if (addr == 0 || buflen == 0) {
        return -EFAULT;
    }

    for (i = 0; i + 1 < buflen; i++) {
        int ret = read_mem(pid, addr + (uint32_t)i, &buf[i], 1);
        if (ret < 0) {
            return ret;
        }
        if (buf[i] == '\0') {
            return 0;
        }
    }

    buf[buflen - 1] = '\0';
    return -ENAMETOOLONG;
}

static int
read_guest_args(pid_t pid, uint32_t sp, uint32_t args[6])
{
    for (size_t i = 0; i < 6; i++) {
        int ret = read_u32(pid, sp + 4U + (uint32_t)i * 4U, &args[i]);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static void
trace_guest_path(const struct runner *runner, pid_t pid, const char *label,
                 uint32_t addr)
{
    char path[MAX_GUEST_STRING];

    if (!runner->trace) {
        return;
    }
    if (addr == 0) {
        trace_log(runner, pid, "%s: <null>", label);
        return;
    }
    if (read_guest_string(pid, addr, path, sizeof(path)) == 0) {
        trace_log(runner, pid, "%s: %s", label, path);
    } else {
        trace_log(runner, pid, "%s: %#x (unreadable)", label, addr);
    }
}

#if defined(__x86_64__)
static uint32_t
regs_ip(const struct user_regs_struct *regs)
{
    return (uint32_t)regs->rip;
}

static uint32_t
regs_sp(const struct user_regs_struct *regs)
{
    return (uint32_t)regs->rsp;
}

static void
regs_set_ip(struct user_regs_struct *regs, uint32_t ip)
{
    regs->rip = ip;
}

static long
regs_sysno(const struct user_regs_struct *regs)
{
    if ((long)regs->orig_rax >= 0) {
        return (long)(uint32_t)regs->orig_rax;
    }
    return (long)(uint32_t)regs->rax;
}

static long
regs_retval(const struct user_regs_struct *regs)
{
    return (long)(int32_t)(uint32_t)regs->rax;
}

static void
regs_set_linux_syscall(struct user_regs_struct *regs, long nr,
                       const uint32_t args[6], uint32_t int80_ip)
{
    regs->rax = (unsigned long)nr;
    regs->orig_rax = (unsigned long)-1L;
    regs->rbx = args[0];
    regs->rcx = args[1];
    regs->rdx = args[2];
    regs->rsi = args[3];
    regs->rdi = args[4];
    regs->rbp = args[5];
    regs->rip = int80_ip;
}

static void
regs_set_return(struct user_regs_struct *regs, long retval)
{
    regs->rax = (uint32_t)retval;
    regs->orig_rax = (unsigned long)-1L;
}

static void
regs_set_gs(struct user_regs_struct *regs, uint16_t selector)
{
    regs->gs = selector;
}

static uint16_t
regs_get_gs(const struct user_regs_struct *regs)
{
    return (uint16_t)regs->gs;
}
#elif defined(__i386__)
static uint32_t
regs_ip(const struct user_regs_struct *regs)
{
    return (uint32_t)regs->eip;
}

static uint32_t
regs_sp(const struct user_regs_struct *regs)
{
    return (uint32_t)regs->esp;
}

static void
regs_set_ip(struct user_regs_struct *regs, uint32_t ip)
{
    regs->eip = ip;
}

static long
regs_sysno(const struct user_regs_struct *regs)
{
    if ((long)regs->orig_eax >= 0) {
        return (long)(uint32_t)regs->orig_eax;
    }
    return (long)(uint32_t)regs->eax;
}

static long
regs_retval(const struct user_regs_struct *regs)
{
    return (long)(int32_t)(uint32_t)regs->eax;
}

static void
regs_set_linux_syscall(struct user_regs_struct *regs, long nr,
                       const uint32_t args[6], uint32_t int80_ip)
{
    uint32_t saved_ebp = regs->ebp;

    regs->eax = (uint32_t)nr;
    regs->orig_eax = (uint32_t)-1;
    regs->ebx = args[0];
    regs->ecx = args[1];
    regs->edx = args[2];
    regs->esi = args[3];
    regs->edi = args[4];
    regs->ebp = (nr == LINUX32_NR_MMAP2) ? args[5] : saved_ebp;
    regs->eip = int80_ip;
}

static void
regs_set_return(struct user_regs_struct *regs, long retval)
{
    regs->eax = (uint32_t)retval;
    regs->orig_eax = (uint32_t)-1;
}

static void
regs_set_gs(struct user_regs_struct *regs, uint16_t selector)
{
    regs->xgs = selector;
}

static uint16_t
regs_get_gs(const struct user_regs_struct *regs)
{
    return (uint16_t)regs->xgs;
}
#else
#error "substrate-run currently supports x86 Linux hosts only"
#endif

static int
get_regs(pid_t pid, struct user_regs_struct *regs)
{
    if (ptrace(PTRACE_GETREGS, pid, 0, regs) < 0) {
        return -host_errno();
    }
    return 0;
}

static int
set_regs(pid_t pid, const struct user_regs_struct *regs)
{
    if (ptrace(PTRACE_SETREGS, pid, 0, (void *)regs) < 0) {
        return -host_errno();
    }
    return 0;
}

static void
trace_stop_state(const struct runner *runner, pid_t pid, int sig)
{
    struct user_regs_struct regs;
    siginfo_t info;
    int ret;

    if (!runner->trace) {
        return;
    }

    ret = get_regs(pid, &regs);
    if (ret < 0) {
        trace_log(runner, pid, "signal %d (failed to read registers: %d)",
                  sig, ret);
        return;
    }

    memset(&info, 0, sizeof(info));
    if (ptrace(PTRACE_GETSIGINFO, pid, 0, &info) == 0) {
#if defined(__x86_64__)
        trace_log(runner, pid,
                  "signal %d code=%d addr=%p eip=%#x esp=%#x eax=%#x ebx=%#x ecx=%#x edx=%#x esi=%#x edi=%#x ebp=%#x gs=%#llx gs_base=%#llx",
                  sig, info.si_code, info.si_addr, (uint32_t)regs.rip,
                  (uint32_t)regs.rsp, (uint32_t)regs.rax, (uint32_t)regs.rbx,
                  (uint32_t)regs.rcx, (uint32_t)regs.rdx, (uint32_t)regs.rsi,
                  (uint32_t)regs.rdi, (uint32_t)regs.rbp, regs.gs,
                  regs.gs_base);
#elif defined(__i386__)
        trace_log(runner, pid,
                  "signal %d code=%d addr=%p eip=%#x esp=%#x eax=%#x ebx=%#x ecx=%#x edx=%#x esi=%#x edi=%#x ebp=%#x gs=%#x",
                  sig, info.si_code, info.si_addr, regs.eip, regs.esp,
                  regs.eax, regs.ebx, regs.ecx, regs.edx, regs.esi, regs.edi,
                  regs.ebp, regs.xgs);
#endif
    } else {
#if defined(__x86_64__)
        trace_log(runner, pid,
                  "signal %d eip=%#x esp=%#x eax=%#x ebx=%#x ecx=%#x edx=%#x esi=%#x edi=%#x ebp=%#x gs=%#llx gs_base=%#llx",
                  sig, (uint32_t)regs.rip, (uint32_t)regs.rsp,
                  (uint32_t)regs.rax, (uint32_t)regs.rbx, (uint32_t)regs.rcx,
                  (uint32_t)regs.rdx, (uint32_t)regs.rsi, (uint32_t)regs.rdi,
                  (uint32_t)regs.rbp, regs.gs, regs.gs_base);
#elif defined(__i386__)
        trace_log(runner, pid,
                  "signal %d eip=%#x esp=%#x eax=%#x ebx=%#x ecx=%#x edx=%#x esi=%#x edi=%#x ebp=%#x gs=%#x",
                  sig, regs.eip, regs.esp, regs.eax, regs.ebx, regs.ecx,
                  regs.edx, regs.esi, regs.edi, regs.ebp, regs.xgs);
#endif
    }
}

static int
check_int80(pid_t pid, uint32_t ip)
{
    unsigned char insn[2];

    if (ip < 2) {
        return -ENOSYS;
    }
    if (read_mem(pid, ip - 2U, insn, sizeof(insn)) < 0) {
        return -ENOSYS;
    }
    if (insn[0] != 0xcd || insn[1] != 0x80) {
        return -ENOSYS;
    }
    return 0;
}

static void
normalize_sysemu_stop(pid_t pid, struct user_regs_struct *regs)
{
    uint32_t ip = regs_ip(regs);
    unsigned char insn[2];

    if (read_mem(pid, ip, insn, sizeof(insn)) == 0 &&
        insn[0] == 0xcd && insn[1] == 0x80) {
        regs_set_ip(regs, ip + 2U);
    }
}

static int
set_breakpoint(pid_t pid, uint32_t addr, unsigned char *saved_byte)
{
    unsigned char trap = 0xcc;
    int ret = read_mem(pid, addr, saved_byte, 1);

    if (ret < 0) {
        return ret;
    }
    return write_mem(pid, addr, &trap, 1);
}

static int
clear_breakpoint(pid_t pid, uint32_t addr, unsigned char saved_byte)
{
    return write_mem(pid, addr, &saved_byte, 1);
}

static int wait_for_specific(pid_t pid, int *status);

static int
remote_load_gs_selector(struct runner *runner, struct tracee *tracee,
                        const struct user_regs_struct *saved, uint16_t selector)
{
    static const unsigned char tmpl[7] = {
        0x66, 0xb8, 0x00, 0x00, 0x8e, 0xe8, 0xcc
    };
    unsigned char code[sizeof(tmpl)];
    unsigned char saved_code[sizeof(tmpl)];
    struct user_regs_struct regs;
    uint32_t ip = regs_ip(saved);
    int status;
    int ret;

    memcpy(code, tmpl, sizeof(code));
    code[2] = (unsigned char)(selector & 0xffU);
    code[3] = (unsigned char)(selector >> 8);

    ret = read_mem(tracee->pid, ip, saved_code, sizeof(saved_code));
    if (ret < 0) {
        return ret;
    }
    ret = write_mem(tracee->pid, ip, code, sizeof(code));
    if (ret < 0) {
        return ret;
    }

    regs = *saved;
    regs_set_gs(&regs, 0);
    ret = set_regs(tracee->pid, &regs);
    if (ret < 0) {
        (void)write_mem(tracee->pid, ip, saved_code, sizeof(saved_code));
        return ret;
    }

    ret = cont_tracee(tracee->pid, PTRACE_CONT, 0);
    if (ret < 0) {
        (void)write_mem(tracee->pid, ip, saved_code, sizeof(saved_code));
        return ret;
    }

    ret = wait_for_specific(tracee->pid, &status);
    if (ret < 0) {
        (void)write_mem(tracee->pid, ip, saved_code, sizeof(saved_code));
        return ret;
    }
    (void)write_mem(tracee->pid, ip, saved_code, sizeof(saved_code));

    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP ||
        ((unsigned int)status >> 16) != 0) {
        trace_log(runner, tracee->pid, "failed to load gs selector %#x", selector);
        return -EIO;
    }

    regs = *saved;
    regs_set_gs(&regs, selector);
    return set_regs(tracee->pid, &regs);
}

static int
wait_for_specific(pid_t pid, int *status)
{
    for (;;) {
        pid_t got = waitpid(pid, status, __WALL);
        if (got == pid) {
            return 0;
        }
        if (got < 0 && errno == EINTR) {
            continue;
        }
        return -host_errno();
    }
}

static int
adopt_fork_child(struct runner *runner, const struct user_regs_struct *saved,
                 struct tracee *parent, pid_t child, uint32_t break_addr,
                 unsigned char saved_break)
{
    int status;
    struct user_regs_struct child_regs;
    struct tracee *tracee;
    int ret;

    if (!runner_consume_pending_stop(runner, child, &status)) {
        ret = wait_for_specific(child, &status);
        if (ret < 0) {
            return ret;
        }
    }
    if (!WIFSTOPPED(status)) {
        return -ECHILD;
    }

    tracee = runner_add(runner, child, true, parent->mode);
    if (!tracee) {
        return -ENOMEM;
    }
    tracee->guest_parent = parent->pid;
    tracee->guest_pgid = parent->guest_pgid;
    tracee_reset_tls(tracee);

    ret = set_ptrace_options(child);
    if (ret < 0) {
        return ret;
    }

    child_regs = *saved;
    regs_set_return(&child_regs, 0);
    (void)clear_breakpoint(child, break_addr, saved_break);
    ret = set_regs(child, &child_regs);
    if (ret < 0) {
        return ret;
    }

    return resume_tracee(runner, tracee, tracee_resume_request(tracee), 0);
}

static int
adopt_event_child(struct runner *runner, struct tracee *parent, pid_t child)
{
    int status;
    struct tracee *tracee;
    int ret;

    ret = wait_for_specific(child, &status);
    if (ret < 0) {
        return ret;
    }
    if (!WIFSTOPPED(status)) {
        return -ECHILD;
    }

    tracee = runner_add(runner, child, true, parent->mode);
    if (!tracee) {
        return -ENOMEM;
    }
    tracee->guest_parent = parent->pid;
    tracee->guest_pgid = parent->guest_pgid;
    tracee_reset_tls(tracee);

    ret = set_ptrace_options(child);
    if (ret < 0) {
        return ret;
    }

    return resume_tracee(runner, tracee, tracee_resume_request(tracee), 0);
}

static enum syscall_action
remote_linux_syscall(struct runner *runner, struct tracee *tracee,
                     const struct user_regs_struct *saved, long linux_nr,
                     const uint32_t args[6], bool may_exec, bool may_fork,
                     long *retval)
{
    pid_t pid = tracee->pid;
    uint32_t ip = regs_ip(saved);
    struct user_regs_struct call_regs;
    pid_t fork_child = -1;
    unsigned char saved_break = 0;
    bool breakpoint_set = false;
    int ret;

    *retval = -ENOSYS;

    if (check_int80(pid, ip) < 0) {
        trace_log(runner, pid, "cannot replay Linux syscall %ld at eip=%#x",
                  linux_nr, ip);
        return SYSCALL_STOPPED;
    }

    ret = set_breakpoint(pid, ip, &saved_break);
    if (ret < 0) {
        *retval = ret;
        return SYSCALL_STOPPED;
    }
    breakpoint_set = true;

    call_regs = *saved;
    regs_set_linux_syscall(&call_regs, linux_nr, args, ip - 2U);
    ret = set_regs(pid, &call_regs);
    if (ret < 0) {
        (void)clear_breakpoint(pid, ip, saved_break);
        *retval = ret;
        return SYSCALL_STOPPED;
    }

    ret = cont_tracee(pid, PTRACE_CONT, 0);
    if (ret < 0) {
        (void)clear_breakpoint(pid, ip, saved_break);
        *retval = ret;
        return SYSCALL_STOPPED;
    }
    tracee->replaying = true;

    for (;;) {
        int status;
        unsigned int event;
        pid_t got;

        if (tracee->pending_status_valid) {
            got = pid;
            status = tracee->pending_status;
            tracee->pending_status_valid = false;
        } else {
            for (;;) {
                got = waitpid(-1, &status, __WALL);
                if (got < 0 && errno == EINTR) {
                    continue;
                }
                break;
            }
        }
        if (got < 0) {
            tracee->replaying = false;
            *retval = -host_errno();
            return SYSCALL_STOPPED;
        }
        if (got != pid) {
            struct tracee *other = runner_find(runner, got);

            if (other && other->replaying && !other->pending_status_valid) {
                other->pending_status = status;
                other->pending_status_valid = true;
            } else {
                runner_note_pending_stop(runner, got, status);
            }
            continue;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            tracee->replaying = false;
            runner_remove(runner, pid, status);
            return SYSCALL_EXITED;
        }

        if (!WIFSTOPPED(status)) {
            continue;
        }

        event = (unsigned int)status >> 16;
        if (event == PTRACE_EVENT_EXEC && may_exec) {
            tracee->replaying = false;
            tracee->ready = true;
            (void)set_ptrace_options(pid);
            return SYSCALL_EXECED;
        }

        if ((event == PTRACE_EVENT_FORK ||
             event == PTRACE_EVENT_VFORK ||
             event == PTRACE_EVENT_CLONE) && may_fork) {
            unsigned long msg = 0;
            if (ptrace(PTRACE_GETEVENTMSG, pid, 0, &msg) == 0) {
                fork_child = (pid_t)msg;
            }
            ret = cont_tracee(pid, PTRACE_CONT, 0);
            if (ret < 0) {
                tracee->replaying = false;
                if (breakpoint_set) {
                    (void)clear_breakpoint(pid, ip, saved_break);
                }
                *retval = ret;
                return SYSCALL_STOPPED;
            }
            continue;
        }

        if (WSTOPSIG(status) == SIGTRAP && event == 0) {
            ret = get_regs(pid, &call_regs);
            if (ret < 0) {
                tracee->replaying = false;
                if (breakpoint_set) {
                    (void)clear_breakpoint(pid, ip, saved_break);
                }
                *retval = ret;
                return SYSCALL_STOPPED;
            }
            *retval = regs_retval(&call_regs);
            trace_log(runner, pid, "Linux syscall %ld returned %ld",
                      linux_nr, *retval);
            if (breakpoint_set) {
                (void)clear_breakpoint(pid, ip, saved_break);
                breakpoint_set = false;
            }
            break;
        }

        ret = cont_tracee(pid, PTRACE_CONT, WSTOPSIG(status));
        if (ret < 0) {
            tracee->replaying = false;
            if (breakpoint_set) {
                (void)clear_breakpoint(pid, ip, saved_break);
            }
            *retval = ret;
            return SYSCALL_STOPPED;
        }
    }
    tracee->replaying = false;

    call_regs = *saved;
    regs_set_return(&call_regs, *retval);
    ret = set_regs(pid, &call_regs);
    if (ret < 0) {
        *retval = ret;
        return SYSCALL_STOPPED;
    }

    if (fork_child > 0) {
        ret = adopt_fork_child(runner, saved, tracee, fork_child, ip,
                               saved_break);
        if (ret < 0) {
            trace_log(runner, pid, "failed to adopt fork child %d: %d",
                      (int)fork_child, ret);
        }
    }

    return SYSCALL_STOPPED;
}

static int
validate_substrate_elf_fd(int fd, const char *path)
{
    struct elf32_ehdr ehdr;
    ssize_t n;

    (void)path;

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -host_errno();
    }

    n = read(fd, &ehdr, sizeof(ehdr));
    if (n != (ssize_t)sizeof(ehdr)) {
        return -ENOEXEC;
    }

    if (memcmp(ehdr.e_ident, "\177ELF", 4) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS32 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_ident[EI_VERSION] != EV_CURRENT ||
        ehdr.e_ident[EI_OSABI] != ELFOSABI_SUBSTRATE ||
        ehdr.e_machine != EM_386 ||
        (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN)) {
        return -ENOEXEC;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -host_errno();
    }
    return 0;
}

static int
load_tls_image_from_fd(int fd, struct tls_image *tls)
{
    struct elf32_ehdr ehdr;
    struct elf32_phdr *phdrs = NULL;
    int ret = -ENOEXEC;

    memset(tls, 0, sizeof(*tls));

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -host_errno();
    }
    if (read(fd, &ehdr, sizeof(ehdr)) != (ssize_t)sizeof(ehdr)) {
        return -ENOEXEC;
    }
    if (memcmp(ehdr.e_ident, "\177ELF", 4) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS32 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_ident[EI_VERSION] != EV_CURRENT) {
        return -ENOEXEC;
    }
    if (ehdr.e_phentsize != sizeof(struct elf32_phdr) || ehdr.e_phnum == 0) {
        return 0;
    }

    phdrs = calloc(ehdr.e_phnum, sizeof(*phdrs));
    if (!phdrs) {
        return -ENOMEM;
    }
    if (lseek(fd, (off_t)ehdr.e_phoff, SEEK_SET) < 0) {
        ret = -host_errno();
        goto out;
    }
    if (read(fd, phdrs, (size_t)ehdr.e_phnum * sizeof(*phdrs)) !=
        (ssize_t)((size_t)ehdr.e_phnum * sizeof(*phdrs))) {
        ret = -ENOEXEC;
        goto out;
    }

    ret = 0;
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        const struct elf32_phdr *ph = &phdrs[i];

        if (ph->p_type != 7U) {
            continue;
        }

        tls->memsz = ph->p_memsz;
        tls->filesz = ph->p_filesz;
        tls->align = ph->p_align ? ph->p_align : 4U;
        if (tls->memsz == 0) {
            break;
        }
        if (tls->filesz > tls->memsz) {
            ret = -ENOEXEC;
            goto out;
        }
        if (tls->filesz != 0) {
            tls->init = malloc(tls->filesz);
            if (!tls->init) {
                ret = -ENOMEM;
                goto out;
            }
            if (lseek(fd, (off_t)ph->p_offset, SEEK_SET) < 0) {
                ret = -host_errno();
                goto out;
            }
            if (read(fd, tls->init, tls->filesz) != (ssize_t)tls->filesz) {
                ret = -ENOEXEC;
                goto out;
            }
        }
        break;
    }

out:
    if (ret < 0) {
        free(tls->init);
        memset(tls, 0, sizeof(*tls));
    }
    free(phdrs);
    return ret;
}

static void
free_tls_image(struct tls_image *tls)
{
    free(tls->init);
    memset(tls, 0, sizeof(*tls));
}

static int
load_tracee_tls_image(pid_t pid, struct tls_image *tls)
{
    char proc_exe[64];
    int fd;
    int ret;

    snprintf(proc_exe, sizeof(proc_exe), "/proc/%d/exe", (int)pid);
    fd = open(proc_exe, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -host_errno();
    }

    ret = load_tls_image_from_fd(fd, tls);
    close(fd);
    return ret;
}

static int
create_memfd_image(void)
{
    int fd = (int)syscall(__NR_memfd_create, "substrate-image", MFD_CLOEXEC);

    if (fd >= 0) {
        return fd;
    }

    char tmpl[] = "/tmp/substrate-run.XXXXXX";
    fd = mkstemp(tmpl);
    if (fd >= 0) {
        unlink(tmpl);
        fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
    }
    return fd;
}

static int
copy_patched_image(int src, int dst)
{
    unsigned char buf[16384];
    off_t off = 0;

    for (;;) {
        ssize_t n = read(src, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -host_errno();
        }
        if (n == 0) {
            break;
        }

        if (off <= EI_OSABI && off + n > EI_OSABI) {
            buf[EI_OSABI - off] = ELFOSABI_SYSV;
        }

        ssize_t done = 0;
        while (done < n) {
            ssize_t w = write(dst, buf + done, (size_t)(n - done));
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -host_errno();
            }
            done += w;
        }

        off += n;
    }

    if (fchmod(dst, 0500) < 0) {
        return -host_errno();
    }
    if (lseek(dst, 0, SEEK_SET) < 0) {
        return -host_errno();
    }
    return 0;
}

static char *
resolve_tracee_path(pid_t pid, const char *path)
{
    char *resolved;
    int n;

    if (pid <= 0) {
        return strdup(path);
    }

    if (path[0] == '/') {
        n = asprintf(&resolved, "/proc/%d/root%s", (int)pid, path);
    } else {
        n = asprintf(&resolved, "/proc/%d/cwd/%s", (int)pid, path);
    }

    if (n < 0) {
        return NULL;
    }
    return resolved;
}

static int
prepare_exec_image(const char *path, pid_t tracee_pid)
{
    char *host_path = resolve_tracee_path(tracee_pid, path);
    int src = -1;
    int dst = -1;
    int ret;

    if (!host_path) {
        return -ENOMEM;
    }

    src = open(host_path, O_RDONLY | O_CLOEXEC);
    if (src < 0) {
        ret = -host_errno();
        goto out;
    }

    ret = validate_substrate_elf_fd(src, path);
    if (ret < 0) {
        goto out;
    }

    dst = create_memfd_image();
    if (dst < 0) {
        ret = -host_errno();
        goto out;
    }

    ret = copy_patched_image(src, dst);
    if (ret < 0) {
        goto out;
    }

    ret = dst;
    dst = -1;

out:
    if (src >= 0) {
        close(src);
    }
    if (dst >= 0) {
        close(dst);
    }
    free(host_path);
    return ret;
}

static void
fill_substrate_stat(struct substrate_stat *dst, const struct stat *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->st_dev = (uint32_t)src->st_dev;
    dst->st_ino = (uint64_t)src->st_ino;
    dst->st_mode = (uint16_t)src->st_mode;
    dst->st_nlink = (uint16_t)src->st_nlink;
    dst->st_uid = (uint16_t)src->st_uid;
    dst->st_gid = (uint16_t)src->st_gid;
    dst->st_rdev = (uint32_t)src->st_rdev;
    dst->st_size = (int64_t)src->st_size;
    dst->st_blksize = (uint32_t)src->st_blksize;
    dst->st_blocks = (int64_t)src->st_blocks;
#if defined(__APPLE__)
    dst->sub_st_atime = (int64_t)src->st_atimespec.tv_sec;
    dst->sub_st_atime_nsec = (uint32_t)src->st_atimespec.tv_nsec;
    dst->sub_st_mtime = (int64_t)src->st_mtimespec.tv_sec;
    dst->sub_st_mtime_nsec = (uint32_t)src->st_mtimespec.tv_nsec;
    dst->sub_st_ctime = (int64_t)src->st_ctimespec.tv_sec;
    dst->sub_st_ctime_nsec = (uint32_t)src->st_ctimespec.tv_nsec;
#else
    dst->sub_st_atime = (int64_t)src->st_atim.tv_sec;
    dst->sub_st_atime_nsec = (uint32_t)src->st_atim.tv_nsec;
    dst->sub_st_mtime = (int64_t)src->st_mtim.tv_sec;
    dst->sub_st_mtime_nsec = (uint32_t)src->st_mtim.tv_nsec;
    dst->sub_st_ctime = (int64_t)src->st_ctim.tv_sec;
    dst->sub_st_ctime_nsec = (uint32_t)src->st_ctim.tv_nsec;
#endif
}

static long
emulate_stat_common(pid_t pid, uint32_t path_addr, uint32_t buf_addr,
                    bool follow)
{
    char guest_path[MAX_GUEST_STRING];
    char *host_path;
    struct stat st;
    struct substrate_stat subst;
    int ret;

    ret = read_guest_string(pid, path_addr, guest_path, sizeof(guest_path));
    if (ret < 0) {
        return ret;
    }

    host_path = resolve_tracee_path(pid, guest_path);
    if (!host_path) {
        return -ENOMEM;
    }

    ret = follow ? stat(host_path, &st) : lstat(host_path, &st);
    if (ret < 0) {
        ret = -host_errno();
        free(host_path);
        return ret;
    }

    free(host_path);
    fill_substrate_stat(&subst, &st);
    ret = write_mem(pid, buf_addr, &subst, sizeof(subst));
    if (ret < 0) {
        return ret;
    }
    return 0;
}

static long
emulate_fstat(pid_t pid, uint32_t fd, uint32_t buf_addr)
{
    char proc_path[64];
    struct stat st;
    struct substrate_stat subst;

    snprintf(proc_path, sizeof(proc_path), "/proc/%d/fd/%u", (int)pid, fd);
    if (stat(proc_path, &st) < 0) {
        return -host_errno();
    }

    fill_substrate_stat(&subst, &st);
    int ret = write_mem(pid, buf_addr, &subst, sizeof(subst));
    return ret < 0 ? ret : 0;
}

static long
emulate_time(pid_t pid, uint32_t tloc)
{
    int64_t now = (int64_t)time(NULL);

    if (tloc != 0) {
        int ret = write_mem(pid, tloc, &now, sizeof(now));
        if (ret < 0) {
            return ret;
        }
    }
    return (long)now;
}

static long
emulate_clock_gettime(pid_t pid, uint32_t clock_id, uint32_t tp_addr)
{
    struct timespec host_ts;
    struct substrate_timespec guest_ts;

    if (clock_gettime((clockid_t)clock_id, &host_ts) < 0) {
        return -host_errno();
    }

    guest_ts.tv_sec = (int64_t)host_ts.tv_sec;
    guest_ts.tv_nsec = (int32_t)host_ts.tv_nsec;
    int ret = write_mem(pid, tp_addr, &guest_ts, sizeof(guest_ts));
    return ret < 0 ? ret : 0;
}

static long
emulate_nanosleep(pid_t pid, uint32_t req_addr, uint32_t rem_addr)
{
    struct substrate_timespec guest_req;
    struct substrate_timespec guest_rem;
    struct timespec req;
    struct timespec rem;
    int ret;

    ret = read_mem(pid, req_addr, &guest_req, sizeof(guest_req));
    if (ret < 0) {
        return ret;
    }
    if (guest_req.tv_sec < 0 || guest_req.tv_nsec < 0 ||
        guest_req.tv_nsec >= 1000000000) {
        return -EINVAL;
    }

    req.tv_sec = (time_t)guest_req.tv_sec;
    req.tv_nsec = guest_req.tv_nsec;
    if (nanosleep(&req, &rem) < 0) {
        if (errno == EINTR && rem_addr != 0) {
            guest_rem.tv_sec = (int64_t)rem.tv_sec;
            guest_rem.tv_nsec = (int32_t)rem.tv_nsec;
            (void)write_mem(pid, rem_addr, &guest_rem, sizeof(guest_rem));
        }
        return -host_errno();
    }
    return 0;
}

static long
emulate_uname(pid_t pid, uint32_t buf_addr)
{
    struct substrate_utsname uts;
    char hostname[sizeof(uts.nodename)];

    memset(&uts, 0, sizeof(uts));
    snprintf(uts.sysname, sizeof(uts.sysname), "%s", "Substrate");
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        snprintf(uts.nodename, sizeof(uts.nodename), "%s", hostname);
    }
    snprintf(uts.release, sizeof(uts.release), "%s", "0.1");
    snprintf(uts.version, sizeof(uts.version), "%s", "Linux runner");
    snprintf(uts.machine, sizeof(uts.machine), "%s", "i386");

    int ret = write_mem(pid, buf_addr, &uts, sizeof(uts));
    return ret < 0 ? ret : 0;
}

static long
remote_scratch_mmap(struct runner *runner, struct tracee *tracee,
                    const struct user_regs_struct *saved)
{
    uint32_t mmap_args[6] = {
        0,
        SCRATCH_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        (uint32_t)-1,
        0,
    };
    long retval;

    if (remote_linux_syscall(runner, tracee, saved, LINUX32_NR_MMAP2, mmap_args,
                             false, false, &retval) == SYSCALL_EXITED) {
        return -EINTR;
    }
    return retval;
}

static uint32_t
align_up_u32(uint32_t value, uint32_t align)
{
    if (align <= 1U) {
        return value;
    }
    return (value + align - 1U) & ~(align - 1U);
}

static int
remote_scratch_munmap(struct runner *runner, struct tracee *tracee,
                      const struct user_regs_struct *saved, uint32_t addr)
{
    uint32_t munmap_args[6] = { 0 };
    long retval;

    munmap_args[0] = addr;
    munmap_args[1] = SCRATCH_SIZE;
    (void)remote_linux_syscall(runner, tracee, saved, 91, munmap_args,
                               false, false, &retval);
    return 0;
}

static int
ensure_tracee_tls(struct runner *runner, struct tracee *tracee,
                  const struct user_regs_struct *saved)
{
    struct tls_image tls;
    struct linux32_user_desc desc;
    struct user_regs_struct regs;
    uint32_t alloc_size;
    uint32_t tls_addr;
    uint32_t tcb_addr;
    uint32_t desc_addr;
    uint32_t self_ptr;
    uint32_t args[6] = { 0 };
    long retval;
    int ret;

    if (tracee->tls_ready) {
        return 0;
    }

    ret = load_tracee_tls_image(tracee->pid, &tls);
    if (ret < 0) {
        return ret;
    }
    if (tls.memsz == 0) {
        tracee->tls_ready = true;
        return 0;
    }

    alloc_size = align_up_u32(tls.memsz + sizeof(uint32_t), 16U) +
                 (uint32_t)sizeof(desc);
    retval = remote_linux_syscall(runner, tracee, saved, LINUX32_NR_MMAP2,
                                  (uint32_t[6]) {
                                      0,
                                      alloc_size,
                                      PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS,
                                      (uint32_t)-1,
                                      0,
                                  },
                                  false, false, &retval) == SYSCALL_EXITED
                 ? -EINTR
                 : retval;
    if (is_errno_ret(retval)) {
        free_tls_image(&tls);
        return (int)retval;
    }

    tls_addr = (uint32_t)retval;
    tcb_addr = tls_addr + tls.memsz;
    desc_addr = tls_addr + align_up_u32(tls.memsz + sizeof(uint32_t), 16U);

    if (tls.filesz != 0) {
        ret = write_mem(tracee->pid, tls_addr, tls.init, tls.filesz);
        if (ret < 0) {
            free_tls_image(&tls);
            return ret;
        }
    }

    self_ptr = tcb_addr;
    ret = write_mem(tracee->pid, tcb_addr, &self_ptr, sizeof(self_ptr));
    if (ret < 0) {
        free_tls_image(&tls);
        return ret;
    }

    memset(&desc, 0, sizeof(desc));
    desc.entry_number = (uint32_t)-1;
    desc.base_addr = tcb_addr;
    desc.limit = 0x000fffffU;
    desc.flags = 0x51U;

    ret = write_mem(tracee->pid, desc_addr, &desc, sizeof(desc));
    if (ret < 0) {
        free_tls_image(&tls);
        return ret;
    }

    args[0] = desc_addr;
    if (remote_linux_syscall(runner, tracee, saved, LINUX32_NR_SET_THREAD_AREA,
                             args, false, false, &retval) == SYSCALL_EXITED) {
        free_tls_image(&tls);
        return -EINTR;
    }
    if (is_errno_ret(retval)) {
        free_tls_image(&tls);
        return (int)retval;
    }

    ret = read_mem(tracee->pid, desc_addr, &desc, sizeof(desc));
    if (ret < 0) {
        free_tls_image(&tls);
        return ret;
    }
    {
        struct linux32_user_desc host_desc = { 0 };
        errno = 0;
        if (ptrace(PTRACE_GET_THREAD_AREA, tracee->pid,
                   (void *)(uintptr_t)desc.entry_number, &host_desc) == 0) {
            trace_log(runner, tracee->pid,
                      "TLS slot %u base=%#x limit=%#x flags=%#x",
                      desc.entry_number, host_desc.base_addr, host_desc.limit,
                      host_desc.flags);
        } else {
            trace_log(runner, tracee->pid,
                      "TLS slot %u read failed: %d",
                      desc.entry_number, host_errno());
        }
    }

    ret = get_regs(tracee->pid, &regs);
    if (ret < 0) {
        free_tls_image(&tls);
        return ret;
    }
    ret = remote_load_gs_selector(runner, tracee, saved,
                                  (uint16_t)((desc.entry_number << 3) | 3U));
    if (ret < 0) {
        free_tls_image(&tls);
        return ret;
    }

    if (tracee->tls_trampoline == 0) {
        retval = remote_linux_syscall(runner, tracee, saved, LINUX32_NR_MMAP2,
                                      (uint32_t[6]) {
                                          0,
                                          4096,
                                          PROT_READ | PROT_WRITE | PROT_EXEC,
                                          MAP_PRIVATE | MAP_ANONYMOUS,
                                          (uint32_t)-1,
                                          0,
                                      },
                                      false, false, &retval) == SYSCALL_EXITED
                     ? -EINTR
                     : retval;
        if (is_errno_ret(retval)) {
            free_tls_image(&tls);
            return (int)retval;
        }
        tracee->tls_trampoline = (uint32_t)retval;
    }

    tracee->tls_ready = true;
    tracee->tls_selector = (uint16_t)((desc.entry_number << 3) | 3U);
    ret = get_regs(tracee->pid, &regs);
    if (ret == 0) {
#if defined(__x86_64__)
        trace_log(runner, tracee->pid,
                  "TLS ready: base=%#x tls=%u selector=%#x tramp=%#x live_gs=%#llx live_gs_base=%#llx",
                  tcb_addr, tls.memsz, (unsigned)((desc.entry_number << 3) | 3U),
                  tracee->tls_trampoline, regs.gs, regs.gs_base);
#else
        trace_log(runner, tracee->pid,
                  "TLS ready: base=%#x tls=%u selector=%#x tramp=%#x live_gs=%#x",
                  tcb_addr, tls.memsz, (unsigned)((desc.entry_number << 3) | 3U),
                  tracee->tls_trampoline, regs.xgs);
#endif
    } else {
        trace_log(runner, tracee->pid,
                  "TLS ready: base=%#x tls=%u selector=%#x tramp=%#x",
                  tcb_addr, tls.memsz, (unsigned)((desc.entry_number << 3) | 3U),
                  tracee->tls_trampoline);
    }
    free_tls_image(&tls);
    return 0;
}

static void
termios_sub_to_linux(struct linux32_termios *dst,
                     const struct substrate_termios *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->c_iflag = src->c_iflag;
    dst->c_oflag = src->c_oflag;
    dst->c_cflag = src->c_cflag;
    dst->c_lflag = src->c_lflag;
    dst->c_line = src->c_line;
    memcpy(dst->c_cc, src->c_cc, LINUX32_NCCS);
}

static uint32_t
sigaction_flags_sub_to_linux(uint32_t flags)
{
    uint32_t out = 0;

    if (flags & SUB_SA_NOCLDSTOP) out |= LINUX_SA_NOCLDSTOP;
    if (flags & SUB_SA_NOCLDWAIT) out |= LINUX_SA_NOCLDWAIT;
    if (flags & SUB_SA_SIGINFO) out |= LINUX_SA_SIGINFO;
    if (flags & SUB_SA_ONSTACK) out |= LINUX_SA_ONSTACK;
    if (flags & SUB_SA_RESTART) out |= LINUX_SA_RESTART;
    if (flags & SUB_SA_NODEFER) out |= LINUX_SA_NODEFER;
    if (flags & SUB_SA_RESETHAND) out |= LINUX_SA_RESETHAND;
    return out;
}

static uint32_t
sigaction_flags_linux_to_sub(uint32_t flags)
{
    uint32_t out = 0;

    if (flags & LINUX_SA_NOCLDSTOP) out |= SUB_SA_NOCLDSTOP;
    if (flags & LINUX_SA_NOCLDWAIT) out |= SUB_SA_NOCLDWAIT;
    if (flags & LINUX_SA_SIGINFO) out |= SUB_SA_SIGINFO;
    if (flags & LINUX_SA_ONSTACK) out |= SUB_SA_ONSTACK;
    if (flags & LINUX_SA_RESTART) out |= SUB_SA_RESTART;
    if (flags & LINUX_SA_NODEFER) out |= SUB_SA_NODEFER;
    if (flags & LINUX_SA_RESETHAND) out |= SUB_SA_RESETHAND;
    return out;
}

static void
termios_linux_to_sub(struct substrate_termios *dst,
                     const struct linux32_termios *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->c_iflag = src->c_iflag;
    dst->c_oflag = src->c_oflag;
    dst->c_cflag = src->c_cflag;
    dst->c_lflag = src->c_lflag;
    dst->c_line = src->c_line;
    memcpy(dst->c_cc, src->c_cc, LINUX32_NCCS);
    dst->c_ispeed = 0;
    dst->c_ospeed = 0;
}

static long
emulate_ioctl(struct runner *runner, struct tracee *tracee,
              const struct user_regs_struct *saved, const uint32_t args[6])
{
    uint32_t fd = args[0];
    uint32_t request = args[1];
    uint32_t argp = args[2];
    uint32_t scratch;
    long retval;
    const char *name = ioctl_name(request);

    if (runner->trace_ioctl) {
        trace_log(runner, tracee->pid, "ioctl fd=%u req=%#x%s%s%s arg=%#x",
                  fd, request, name ? " (" : "", name ? name : "",
                  name ? ")" : "", argp);
    }

    switch (request) {
    case SUB_TCGETS:
    case SUB_TCSETS:
    case SUB_TCSETSW:
    case SUB_TCSETSF:
    case SUB_TIOCGWINSZ:
    case SUB_TIOCSWINSZ:
    case SUB_TIOCGPGRP:
    case SUB_TIOCSPGRP:
    case SUB_FIONREAD:
        break;
    case SUB_TIOCNOTTY:
    case SUB_TIOCSCTTY:
        retval = remote_linux_syscall(runner, tracee, saved, SUB_SYS_IOCTL, args,
                                      false, false, &retval) == SYSCALL_EXITED
                     ? -EINTR
                     : retval;
        if (runner->trace_ioctl) {
            trace_log(runner, tracee->pid, "ioctl fd=%u req=%#x -> %ld",
                      fd, request, retval);
        }
        return retval;
    default:
        retval = remote_linux_syscall(runner, tracee, saved, SUB_SYS_IOCTL, args,
                                      false, false, &retval) == SYSCALL_EXITED
                     ? -EINTR
                     : retval;
        if (runner->trace_ioctl) {
            trace_log(runner, tracee->pid, "ioctl fd=%u req=%#x -> %ld",
                      fd, request, retval);
        }
        return retval;
    }

    retval = remote_scratch_mmap(runner, tracee, saved);
    if (is_errno_ret(retval)) {
        if (runner->trace_ioctl) {
            trace_log(runner, tracee->pid, "ioctl fd=%u req=%#x -> %ld",
                      fd, request, retval);
        }
        return retval;
    }
    scratch = (uint32_t)retval;

    if (request == SUB_TCGETS) {
        uint32_t ioctl_args[6] = { fd, request, scratch, 0, 0, 0 };
        struct linux32_termios linux_tio;
        struct substrate_termios sub_tio;

        if (remote_linux_syscall(runner, tracee, saved, SUB_SYS_IOCTL,
                                 ioctl_args, false, false, &retval) == SYSCALL_EXITED) {
            return -EINTR;
        }
        if (!is_errno_ret(retval)) {
            if (read_mem(tracee->pid, scratch, &linux_tio, sizeof(linux_tio)) == 0) {
                termios_linux_to_sub(&sub_tio, &linux_tio);
                (void)write_mem(tracee->pid, argp, &sub_tio, sizeof(sub_tio));
            }
        }
        remote_scratch_munmap(runner, tracee, saved, scratch);
        if (runner->trace_ioctl) {
            trace_log(runner, tracee->pid, "ioctl fd=%u req=%#x -> %ld",
                      fd, request, retval);
        }
        return retval;
    }

    if (request == SUB_TCSETS || request == SUB_TCSETSW || request == SUB_TCSETSF) {
        uint32_t ioctl_args[6] = { fd, request, scratch, 0, 0, 0 };
        struct substrate_termios sub_tio;
        struct linux32_termios linux_tio;

        if (read_mem(tracee->pid, argp, &sub_tio, sizeof(sub_tio)) < 0) {
            remote_scratch_munmap(runner, tracee, saved, scratch);
            return -EFAULT;
        }
        termios_sub_to_linux(&linux_tio, &sub_tio);
        (void)write_mem(tracee->pid, scratch, &linux_tio, sizeof(linux_tio));
        if (remote_linux_syscall(runner, tracee, saved, SUB_SYS_IOCTL,
                                 ioctl_args, false, false, &retval) == SYSCALL_EXITED) {
            return -EINTR;
        }
        remote_scratch_munmap(runner, tracee, saved, scratch);
        if (runner->trace_ioctl) {
            trace_log(runner, tracee->pid, "ioctl fd=%u req=%#x -> %ld",
                      fd, request, retval);
        }
        return retval;
    }

    if (request == SUB_TIOCGWINSZ) {
        uint32_t ioctl_args[6] = { fd, request, scratch, 0, 0, 0 };
        struct linux32_winsize wsz;

        if (remote_linux_syscall(runner, tracee, saved, SUB_SYS_IOCTL,
                                 ioctl_args, false, false, &retval) == SYSCALL_EXITED) {
            return -EINTR;
        }
        if (!is_errno_ret(retval)) {
            if (read_mem(tracee->pid, scratch, &wsz, sizeof(wsz)) == 0) {
                (void)write_mem(tracee->pid, argp, &wsz, sizeof(wsz));
            }
        }
        remote_scratch_munmap(runner, tracee, saved, scratch);
        if (runner->trace_ioctl) {
            trace_log(runner, tracee->pid, "ioctl fd=%u req=%#x -> %ld",
                      fd, request, retval);
        }
        return retval;
    }

    if (request == SUB_TIOCSWINSZ) {
        uint32_t ioctl_args[6] = { fd, request, scratch, 0, 0, 0 };
        struct linux32_winsize wsz;

        if (read_mem(tracee->pid, argp, &wsz, sizeof(wsz)) < 0) {
            remote_scratch_munmap(runner, tracee, saved, scratch);
            return -EFAULT;
        }
        (void)write_mem(tracee->pid, scratch, &wsz, sizeof(wsz));
        if (remote_linux_syscall(runner, tracee, saved, SUB_SYS_IOCTL,
                                 ioctl_args, false, false, &retval) == SYSCALL_EXITED) {
            return -EINTR;
        }
        remote_scratch_munmap(runner, tracee, saved, scratch);
        return retval;
    }

    if (request == SUB_TIOCGPGRP || request == SUB_TIOCSPGRP || request == SUB_FIONREAD) {
        uint32_t ioctl_args[6] = { fd, request, scratch, 0, 0, 0 };
        int32_t value = 0;

        if (request == SUB_TIOCSPGRP || request == SUB_FIONREAD) {
            if (read_mem(tracee->pid, argp, &value, sizeof(value)) < 0) {
                remote_scratch_munmap(runner, tracee, saved, scratch);
                return -EFAULT;
            }
            (void)write_mem(tracee->pid, scratch, &value, sizeof(value));
        }

        if (remote_linux_syscall(runner, tracee, saved, SUB_SYS_IOCTL,
                                 ioctl_args, false, false, &retval) == SYSCALL_EXITED) {
            return -EINTR;
        }

        if (!is_errno_ret(retval) && request == SUB_TIOCGPGRP) {
            if (read_mem(tracee->pid, scratch, &value, sizeof(value)) == 0) {
                (void)write_mem(tracee->pid, argp, &value, sizeof(value));
            }
        }
        remote_scratch_munmap(runner, tracee, saved, scratch);
        return retval;
    }

    remote_scratch_munmap(runner, tracee, saved, scratch);
    if (runner->trace_ioctl) {
        trace_log(runner, tracee->pid, "ioctl fd=%u req=%#x -> -ENOTTY",
                  fd, request);
    }
    return -ENOTTY;
}

static long
emulate_sigaction(struct runner *runner, struct tracee *tracee,
                  const struct user_regs_struct *saved, const uint32_t args[6])
{
    struct substrate_sigaction sub_act;
    struct substrate_sigaction sub_old;
    struct linux32_old_sigaction linux_act;
    struct linux32_old_sigaction linux_old;
    uint32_t scratch;
    uint32_t call_args[6] = { 0 };
    long retval;
    int ret;

    retval = remote_scratch_mmap(runner, tracee, saved);
    if (is_errno_ret(retval)) {
        return retval;
    }
    scratch = (uint32_t)retval;

    call_args[0] = args[0];
    if (args[1] != 0) {
        ret = read_mem(tracee->pid, args[1], &sub_act, sizeof(sub_act));
        if (ret < 0) {
            remote_scratch_munmap(runner, tracee, saved, scratch);
            return ret;
        }
        memset(&linux_act, 0, sizeof(linux_act));
        linux_act.handler = sub_act.handler;
        linux_act.mask = sub_act.mask;
        linux_act.flags = sigaction_flags_sub_to_linux(sub_act.flags);
        ret = write_mem(tracee->pid, scratch, &linux_act, sizeof(linux_act));
        if (ret < 0) {
            remote_scratch_munmap(runner, tracee, saved, scratch);
            return ret;
        }
        call_args[1] = scratch;
    }
    if (args[2] != 0) {
        call_args[2] = scratch + 64U;
    }

    if (remote_linux_syscall(runner, tracee, saved, SUB_SYS_SIGACTION,
                             call_args, false, false, &retval) == SYSCALL_EXITED) {
        remote_scratch_munmap(runner, tracee, saved, scratch);
        return -EINTR;
    }

    if (!is_errno_ret(retval) && args[2] != 0) {
        ret = read_mem(tracee->pid, call_args[2], &linux_old, sizeof(linux_old));
        if (ret < 0) {
            remote_scratch_munmap(runner, tracee, saved, scratch);
            return ret;
        }
        sub_old.handler = linux_old.handler;
        sub_old.mask = linux_old.mask;
        sub_old.flags = sigaction_flags_linux_to_sub(linux_old.flags);
        ret = write_mem(tracee->pid, args[2], &sub_old, sizeof(sub_old));
        if (ret < 0) {
            remote_scratch_munmap(runner, tracee, saved, scratch);
            return ret;
        }
    }

    remote_scratch_munmap(runner, tracee, saved, scratch);
    return retval;
}

static long
emulate_sigprocmask(struct runner *runner, struct tracee *tracee,
                    const struct user_regs_struct *saved, const uint32_t args[6])
{
    uint32_t call_args[6];
    long retval;

    memcpy(call_args, args, sizeof(call_args));
    switch (args[0]) {
    case SUB_SIG_BLOCK:
        call_args[0] = LINUX_SIG_BLOCK;
        break;
    case SUB_SIG_UNBLOCK:
        call_args[0] = LINUX_SIG_UNBLOCK;
        break;
    case SUB_SIG_SETMASK:
        call_args[0] = LINUX_SIG_SETMASK;
        break;
    default:
        return -EINVAL;
    }

    if (remote_linux_syscall(runner, tracee, saved, SUB_SYS_SIGPROCMASK,
                             call_args, false, false, &retval) == SYSCALL_EXITED) {
        return -EINTR;
    }
    return retval;
}

static long
emulate_execve(struct runner *runner, struct tracee *tracee,
               const struct user_regs_struct *saved, const uint32_t args[6])
{
    char guest_path[MAX_GUEST_STRING];
    char proc_fd_path[64];
    size_t proc_fd_len;
    uint32_t scratch;
    uint32_t empty_addr;
    uint32_t open_args[6] = { 0 };
    uint32_t exec_args[6] = { 0 };
    uint32_t close_args[6] = { 0 };
    uint32_t munmap_args[6] = { 0 };
    long retval;
    int image_fd;
    int ret;

    ret = read_guest_string(tracee->pid, args[0], guest_path, sizeof(guest_path));
    if (ret < 0) {
        return ret;
    }

    image_fd = prepare_exec_image(guest_path, tracee->pid);
    if (image_fd < 0 && should_fallback_native_exec(image_fd)) {
        enum syscall_action action;

        if (runner->trace) {
            trace_log(runner, tracee->pid,
                      "execve fallback to native Linux image: %s (%d)",
                      guest_path, image_fd);
        }
        action = remote_linux_syscall(runner, tracee, saved, SUB_SYS_EXECVE,
                                      args, true, false, &retval);
        if (action == SYSCALL_EXECED) {
            tracee->mode = TRACEE_LINUX;
            tracee_reset_tls(tracee);
            return LONG_MIN;
        }
        if (action == SYSCALL_EXITED) {
            return LONG_MIN + 1;
        }
        return retval;
    }
    if (image_fd < 0) {
        return image_fd;
    }

    retval = remote_scratch_mmap(runner, tracee, saved);
    if (is_errno_ret(retval)) {
        close(image_fd);
        return retval;
    }
    scratch = (uint32_t)retval;

    snprintf(proc_fd_path, sizeof(proc_fd_path), "/proc/%d/fd/%d",
             (int)getpid(), image_fd);
    proc_fd_len = strlen(proc_fd_path) + 1U;
    empty_addr = scratch + (uint32_t)proc_fd_len;

    ret = write_mem(tracee->pid, scratch, proc_fd_path, proc_fd_len);
    if (ret == 0) {
        char nul = '\0';
        ret = write_mem(tracee->pid, empty_addr, &nul, 1);
    }
    if (ret < 0) {
        close(image_fd);
        return ret;
    }

    open_args[0] = scratch;
    open_args[1] = O_RDONLY | O_CLOEXEC;
    if (remote_linux_syscall(runner, tracee, saved, 5, open_args,
                             false, false, &retval) == SYSCALL_EXITED) {
        close(image_fd);
        return -EINTR;
    }
    if (retval < 0) {
        close(image_fd);
        return retval;
    }

    exec_args[0] = (uint32_t)retval;
    exec_args[1] = empty_addr;
    exec_args[2] = args[1];
    exec_args[3] = args[2];
    exec_args[4] = AT_EMPTY_PATH;

    enum syscall_action action = remote_linux_syscall(
        runner, tracee, saved, LINUX32_NR_EXECVEAT, exec_args, true, false, &retval);
    close(image_fd);

    if (action == SYSCALL_EXECED) {
        tracee->mode = TRACEE_SUBSTRATE;
        tracee_reset_tls(tracee);
        return LONG_MIN;
    }
    if (action == SYSCALL_EXITED) {
        return LONG_MIN + 1;
    }

    close_args[0] = exec_args[0];
    (void)remote_linux_syscall(runner, tracee, saved, 6, close_args,
                               false, false, &retval);

    munmap_args[0] = scratch;
    munmap_args[1] = SCRATCH_SIZE;
    (void)remote_linux_syscall(runner, tracee, saved, 91, munmap_args,
                               false, false, &retval);
    return retval;
}

static long
emulate_setpgid(struct runner *runner, struct tracee *tracee,
                const struct user_regs_struct *saved, const uint32_t args[6])
{
    long retval;
    pid_t target_pid;
    pid_t target_pgid;
    struct tracee *target;

    if (remote_linux_syscall(runner, tracee, saved, LINUX32_NR_SETPGID, args,
                             false, false, &retval) == SYSCALL_EXITED) {
        return -EINTR;
    }
    if (retval < 0) {
        return retval;
    }

    target_pid = (pid_t)(int32_t)args[0];
    if (target_pid == 0) {
        target_pid = tracee->pid;
    }
    target_pgid = (pid_t)(int32_t)args[1];
    if (target_pgid == 0) {
        target_pgid = target_pid;
    }

    target = runner_find(runner, target_pid);
    if (target) {
        target->guest_pgid = target_pgid;
    }
    return retval;
}

static long
emulate_wait_common(struct runner *runner, struct tracee *tracee,
                    const uint32_t args[6], bool with_rusage)
{
    pid_t target = (pid_t)(int32_t)args[0];
    uint32_t status_addr = args[1];
    int options = (int)(int32_t)args[2];
    uint32_t rusage_addr = with_rusage ? args[3] : 0;
    pid_t caller_pgid = tracee->guest_pgid > 0 ? tracee->guest_pgid : tracee->pid;
    pid_t child_pid;
    int child_status;

    if (runner_consume_child_event(runner, tracee->pid, target, options,
                                   caller_pgid, &child_pid, &child_status)) {
        goto deliver;
    }
    if (!runner_has_matching_child(runner, tracee->pid, target, caller_pgid)) {
        return -ECHILD;
    }
    if (options & WNOHANG) {
        return 0;
    }

    for (;;) {
        int status;
        pid_t got;

        if (runner_dispatch_pending_stop(runner)) {
            if (runner_consume_child_event(runner, tracee->pid, target, options,
                                           caller_pgid, &child_pid,
                                           &child_status)) {
                break;
            }
            if (!runner_has_matching_child(runner, tracee->pid, target,
                                           caller_pgid)) {
                return -ECHILD;
            }
            continue;
        }

        got = waitpid(-1, &status, __WALL);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD &&
                !runner_has_matching_child(runner, tracee->pid, target,
                                           caller_pgid)) {
                return -ECHILD;
            }
            return -host_errno();
        }

        {
            struct tracee *other = runner_find(runner, got);

            if (other && other->replaying && !other->pending_status_valid) {
                other->pending_status = status;
                other->pending_status_valid = true;
            } else {
                runner_note_pending_stop(runner, got, status);
            }
        }

        if (runner_consume_child_event(runner, tracee->pid, target, options,
                                       caller_pgid, &child_pid, &child_status)) {
            break;
        }
        if (!runner_has_matching_child(runner, tracee->pid, target,
                                       caller_pgid)) {
            return -ECHILD;
        }
    }

deliver:
    if (status_addr != 0) {
        int ret = write_mem(tracee->pid, status_addr, &child_status,
                            sizeof(child_status));
        if (ret < 0) {
            return ret;
        }
    }
    if (rusage_addr != 0) {
        struct rusage ru;

        memset(&ru, 0, sizeof(ru));
        if (write_mem(tracee->pid, rusage_addr, &ru, sizeof(ru)) < 0) {
            return -EFAULT;
        }
    }
    return child_pid;
}

static long
emulate_kill(struct runner *runner, struct tracee *tracee,
             const struct user_regs_struct *saved, const uint32_t args[6])
{
    pid_t target = (pid_t)(int32_t)args[0];
    int sig = (int)(int32_t)args[1];
    pid_t caller_pgid = tracee->guest_pgid > 0 ? tracee->guest_pgid : tracee->pid;
    long retval;

    if (remote_linux_syscall(runner, tracee, saved, SUB_SYS_KILL, args,
                             false, false, &retval) == SYSCALL_EXITED) {
        return -EINTR;
    }
    if (retval < 0 || sig != SIGCONT) {
        return retval;
    }

    for (size_t i = 0; i < ARRAY_SIZE(runner->tracees); i++) {
        struct tracee *child = &runner->tracees[i];

        if (!child->live || !child->group_stopped) {
            continue;
        }
        if (!signal_target_matches(target, child->pid, child->guest_pgid,
                                   caller_pgid)) {
            continue;
        }
        child->group_stopped = false;
        if (resume_tracee(runner, child, tracee_resume_request(child), 0) < 0) {
            child->group_stopped = true;
            if (runner->trace) {
                trace_log(runner, child->pid,
                          "failed to resume stopped tracee after SIGCONT");
            }
        } else if (runner->trace) {
            trace_log(runner, child->pid, "resumed after SIGCONT");
        }
    }

    return retval;
}

static bool
direct_linux_number(long sub_nr, long *linux_nr)
{
    switch (sub_nr) {
    case SUB_SYS_EXIT:
    case SUB_SYS_READ:
    case SUB_SYS_WRITE:
    case SUB_SYS_OPEN:
    case SUB_SYS_CLOSE:
    case SUB_SYS_WAITPID:
    case SUB_SYS_CREAT:
    case SUB_SYS_LINK:
    case SUB_SYS_UNLINK:
    case SUB_SYS_CHDIR:
    case SUB_SYS_MKNOD:
    case SUB_SYS_CHMOD:
    case SUB_SYS_LSEEK:
    case SUB_SYS_GETPID:
    case SUB_SYS_MOUNT:
    case SUB_SYS_UMOUNT:
    case SUB_SYS_SETUID:
    case SUB_SYS_GETUID:
    case SUB_SYS_STIME:
    case SUB_SYS_ALARM:
    case SUB_SYS_ACCESS:
    case SUB_SYS_SYNC:
    case SUB_SYS_KILL:
    case SUB_SYS_RENAME:
    case SUB_SYS_MKDIR:
    case SUB_SYS_RMDIR:
    case SUB_SYS_DUP:
    case SUB_SYS_PIPE:
    case SUB_SYS_TIMES:
    case SUB_SYS_BRK:
    case SUB_SYS_SETGID:
    case SUB_SYS_GETGID:
    case SUB_SYS_SIGNAL:
    case SUB_SYS_GETEUID:
    case SUB_SYS_GETEGID:
    case SUB_SYS_ACCT:
    case SUB_SYS_IOCTL:
    case SUB_SYS_FCNTL:
    case SUB_SYS_UMASK:
    case SUB_SYS_CHROOT:
    case SUB_SYS_DUP2:
    case SUB_SYS_GETPPID:
    case SUB_SYS_SYMLINK:
    case SUB_SYS_READLINK:
    case SUB_SYS_REBOOT:
    case SUB_SYS_MUNMAP:
    case SUB_SYS_TRUNCATE:
    case SUB_SYS_FTRUNCATE:
    case SUB_SYS_FCHMOD:
    case SUB_SYS_FCHOWN:
    case SUB_SYS_SETPRIORITY:
    case SUB_SYS_GETPRIORITY:
    case SUB_SYS_WAIT4:
    case SUB_SYS_GETDENTS:
    case SUB_SYS_MSYNC:
    case SUB_SYS_MLOCK:
    case SUB_SYS_MUNLOCK:
    case SUB_SYS_GETCWD:
        *linux_nr = sub_nr;
        return true;
    case SUB_SYS_POLL:
        *linux_nr = LINUX32_NR_POLL;
        return true;
    case SUB_SYS_SETSID:
        *linux_nr = LINUX32_NR_SETSID;
        return true;
    case SUB_SYS_SETPGID:
        *linux_nr = LINUX32_NR_SETPGID;
        return true;
    case SUB_SYS_GETPGID:
        *linux_nr = LINUX32_NR_GETPGID;
        return true;
    case SUB_SYS_GETSID:
        *linux_nr = LINUX32_NR_GETSID;
        return true;
    case SUB_SYS_GETRUSAGE:
        *linux_nr = LINUX32_NR_GETRUSAGE;
        return true;
    default:
        return false;
    }
}

static int
finish_syscall(pid_t pid, const struct user_regs_struct *saved, long retval)
{
    struct user_regs_struct regs = *saved;

    regs_set_return(&regs, retval);
    return set_regs(pid, &regs);
}

static enum syscall_action
handle_substrate_syscall(struct runner *runner, struct tracee *tracee)
{
    struct user_regs_struct saved;
    uint32_t args[6] = { 0 };
    long sub_nr;
    long linux_nr;
    long retval = -ENOSYS;
    int ret;

    ret = get_regs(tracee->pid, &saved);
    if (ret < 0) {
        return SYSCALL_EXITED;
    }
    normalize_sysemu_stop(tracee->pid, &saved);
    trace_log(runner, tracee->pid, "pre-syscall regs: nr=%ld eax=%#lx orig=%#lx eip=%#x",
              regs_sysno(&saved), (unsigned long)regs_retval(&saved),
#if defined(__x86_64__)
              (unsigned long)saved.orig_rax,
#elif defined(__i386__)
              (unsigned long)saved.orig_eax,
#endif
              regs_ip(&saved));

    ret = ensure_tracee_tls(runner, tracee, &saved);
    if (ret < 0) {
        retval = ret;
        goto stopped;
    }
    if (tracee->tls_ready) {
        struct user_regs_struct live_regs;

        ret = get_regs(tracee->pid, &live_regs);
        if (ret < 0) {
            return SYSCALL_EXITED;
        }
        regs_set_gs(&saved, regs_get_gs(&live_regs));
    }

    sub_nr = regs_sysno(&saved);
    ret = read_guest_args(tracee->pid, regs_sp(&saved), args);
    if (ret < 0) {
        retval = ret;
        goto stopped;
    }

    trace_log(runner, tracee->pid,
              "syscall %ld(%#x,%#x,%#x,%#x,%#x,%#x)",
              sub_nr, args[0], args[1], args[2], args[3], args[4], args[5]);
    switch (sub_nr) {
    case SUB_SYS_OPEN:
    case SUB_SYS_CHDIR:
    case SUB_SYS_ACCESS:
    case SUB_SYS_STAT:
    case SUB_SYS_LSTAT:
    case SUB_SYS_READLINK:
    case SUB_SYS_MKDIR:
    case SUB_SYS_RMDIR:
    case SUB_SYS_TRUNCATE:
    case SUB_SYS_CHMOD:
    case SUB_SYS_LCHOWN:
    case SUB_SYS_EXECVE:
        trace_guest_path(runner, tracee->pid, "path", args[0]);
        break;
    case SUB_SYS_LINK:
    case SUB_SYS_SYMLINK:
    case SUB_SYS_RENAME:
        trace_guest_path(runner, tracee->pid, "path1", args[0]);
        trace_guest_path(runner, tracee->pid, "path2", args[1]);
        break;
    default:
        break;
    }

    switch (sub_nr) {
    case SUB_SYS_FORK:
        return remote_linux_syscall(runner, tracee, &saved, 2, args,
                                    false, true, &retval);
    case SUB_SYS_EXECVE:
        retval = emulate_execve(runner, tracee, &saved, args);
        if (retval == LONG_MIN) {
            return SYSCALL_EXECED;
        }
        if (retval == LONG_MIN + 1) {
            return SYSCALL_EXITED;
        }
        break;
    case SUB_SYS_TIME:
        retval = emulate_time(tracee->pid, args[0]);
        break;
    case SUB_SYS_STAT:
        retval = emulate_stat_common(tracee->pid, args[0], args[1], true);
        break;
    case SUB_SYS_LSTAT:
        retval = emulate_stat_common(tracee->pid, args[0], args[1], false);
        break;
    case SUB_SYS_FSTAT:
        retval = emulate_fstat(tracee->pid, args[0], args[1]);
        break;
    case SUB_SYS_UNAME:
        retval = emulate_uname(tracee->pid, args[0]);
        break;
    case SUB_SYS_NANOSLEEP:
        retval = emulate_nanosleep(tracee->pid, args[0], args[1]);
        break;
    case SUB_SYS_CLOCK_GETTIME:
        retval = emulate_clock_gettime(tracee->pid, args[0], args[1]);
        break;
    case SUB_SYS_IOCTL:
        retval = emulate_ioctl(runner, tracee, &saved, args);
        if (retval == -EINTR) {
            return SYSCALL_EXITED;
        }
        break;
    case SUB_SYS_SIGACTION:
        retval = emulate_sigaction(runner, tracee, &saved, args);
        if (retval == -EINTR) {
            return SYSCALL_EXITED;
        }
        break;
    case SUB_SYS_SIGPROCMASK:
        retval = emulate_sigprocmask(runner, tracee, &saved, args);
        if (retval == -EINTR) {
            return SYSCALL_EXITED;
        }
        break;
    case SUB_SYS_WAITPID:
        retval = emulate_wait_common(runner, tracee, args, false);
        break;
    case SUB_SYS_WAIT4:
        retval = emulate_wait_common(runner, tracee, args, true);
        break;
    case SUB_SYS_KILL:
        retval = emulate_kill(runner, tracee, &saved, args);
        if (retval == -EINTR) {
            return SYSCALL_EXITED;
        }
        break;
    case SUB_SYS_MMAP:
        return remote_linux_syscall(runner, tracee, &saved, LINUX32_NR_MMAP2, args,
                                    false, false, &retval);
    case SUB_SYS_SETPGID:
        retval = emulate_setpgid(runner, tracee, &saved, args);
        if (retval == -EINTR) {
            return SYSCALL_EXITED;
        }
        break;
    case SUB_SYS_PTRACE:
    case SUB_SYS_SIGRETURN:
    case SUB_SYS_CLONE:
    case SUB_SYS_MODIFY_LDT:
    case SUB_SYS_SIGALTSTACK:
    case SUB_SYS_SYSCTL:
    case SUB_SYS_FUTEX:
    case SUB_SYS_GETGROUPS:
    case SUB_SYS_SETGROUPS:
    case SUB_SYS_STATFS:
    case SUB_SYS_FSTATFS:
        retval = -ENOSYS;
        break;
    default:
        if (direct_linux_number(sub_nr, &linux_nr)) {
            return remote_linux_syscall(runner, tracee, &saved, linux_nr, args,
                                        false, false, &retval);
        }
        retval = -ENOSYS;
        break;
    }

stopped:
    ret = finish_syscall(tracee->pid, &saved, retval);
    if (ret < 0) {
        return SYSCALL_EXITED;
    }
    return SYSCALL_STOPPED;
}

static int
start_tracee(struct runner *runner, char *const argv[], char *const envp[])
{
    int image_fd = prepare_exec_image(argv[0], 0);
    pid_t child;
    int status;

    if (image_fd < 0) {
        errno = -image_fd;
        perror("substrate-run: prepare executable image");
        return -1;
    }

    child = fork();
    if (child < 0) {
        perror("substrate-run: fork");
        close(image_fd);
        return -1;
    }

    if (child == 0) {
        if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
            perror("substrate-run: ptrace TRACEME");
            _exit(127);
        }
        raise(SIGSTOP);
        fexecve(image_fd, argv, envp);
        perror("substrate-run: fexecve");
        _exit(errno == ENOEXEC ? 126 : 127);
    }

    close(image_fd);

    if (wait_for_specific(child, &status) < 0 || !WIFSTOPPED(status)) {
        fprintf(stderr, "substrate-run: child did not stop before exec\n");
        return -1;
    }

    runner->root = child;
    runner->root_status = 1;
    if (!runner_add(runner, child, false, TRACEE_SUBSTRATE)) {
        return -1;
    }
    {
        struct tracee *root = runner_find(runner, child);
        if (root) {
            root->guest_pgid = getpgid(child);
            if (root->guest_pgid <= 0) {
                root->guest_pgid = child;
            }
        }
    }

    if (set_ptrace_options(child) < 0) {
        perror("substrate-run: ptrace SETOPTIONS");
        return -1;
    }

    if (cont_tracee(child, PTRACE_CONT, 0) < 0) {
        perror("substrate-run: ptrace CONT");
        return -1;
    }

    return 0;
}

static int
runner_loop(struct runner *runner)
{
    while (runner->live_count > 0) {
        int status;
        pid_t pid;

        if (runner_dispatch_pending_stop(runner)) {
            continue;
        }

        pid = waitpid(-1, &status, __WALL);

        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                break;
            }
            perror("substrate-run: waitpid");
            return 1;
        }

        (void)dispatch_tracee_status(runner, pid, status);
    }

    return runner->root_done ? runner->root_status : 1;
}

int
main(int argc, char *argv[], char *envp[])
{
    struct runner runner;
    int argi = 1;

    memset(&runner, 0, sizeof(runner));
    runner.root = -1;
    runner.root_status = 1;

    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "-t") == 0 ||
            strcmp(argv[argi], "--trace") == 0) {
            runner.trace = true;
            argi++;
        } else if (strcmp(argv[argi], "--ioctl-trace") == 0) {
            runner.trace_ioctl = true;
            argi++;
        } else if (strcmp(argv[argi], "--") == 0) {
            argi++;
            break;
        } else if (strcmp(argv[argi], "-h") == 0 ||
                   strcmp(argv[argi], "--help") == 0) {
            usage(argv[0]);
        } else {
            usage(argv[0]);
        }
    }

    if (argi >= argc) {
        usage(argv[0]);
    }

    if (start_tracee(&runner, &argv[argi], envp) < 0) {
        return 1;
    }

    return runner_loop(&runner);
}
