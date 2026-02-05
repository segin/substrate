#include "exec.h"
#include "expand.h"
#include "shell_var.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <ctype.h>
#include "job.h"
#include "util.h"
#include "parser.h"
#include <signal.h>

int shell_is_interactive = 0;
int shell_errexit = 0;
int errexit_disabled = 0;
pid_t shell_pgid;
struct termios shell_tmodes;
#include <termios.h>

extern int tcsetpgrp(int fd, pid_t pgrp);
extern int dup(int oldfd);
extern int setpgid(pid_t pid, pid_t pgid);
typedef void (*sig_t)(int);
extern sig_t signal(int sig, sig_t func);

#ifndef SIG_DFL
#define SIG_DFL ((sig_t)0)
#endif
#ifndef SIG_IGN
#define SIG_IGN ((sig_t)1)
#endif

#ifndef SIGINT
#define SIGINT 2
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGTSTP
#define SIGTSTP 18
#endif
#ifndef SIGTTIN
#define SIGTTIN 21
#endif
#ifndef SIGTTOU
#define SIGTTOU 22
#endif
#ifndef SIGCHLD
#define SIGCHLD 20
#endif
#include <errno.h>

/* Centralized list of shell builtins for command -v/-V */
static const char *shell_builtins[] = {
    "exit", "cd", "export", "unset", "shift", "set", "exec", "eval",
    "wait", "kill", "jobs", "fg", "bg", "return", "local",
    "echo", "test", "[", ":", ".", "source", "break", "continue",
    "times", "umask", "command", "trap", "read", "readonly", "getopts", NULL
};

static int is_builtin(const char *name) {
    for (int i = 0; shell_builtins[i]; i++) {
        if (strcmp(shell_builtins[i], name) == 0) return 1;
    }
    return 0;
}

/* Signal Traps */
#define EXEC_SIG_MAX 32
static char *trap_commands[EXEC_SIG_MAX] = { NULL };
static volatile sig_atomic_t pending_traps[EXEC_SIG_MAX] = { 0 };
static volatile sig_atomic_t trap_pending_any = 0;

static void trap_handler(int sig) {
    if (sig > 0 && sig < EXEC_SIG_MAX) {
        pending_traps[sig] = 1;
        trap_pending_any = 1;
    }
}

static int parse_signal(const char *str) {
    if (isdigit((unsigned char)str[0])) return atoi(str);
    if (strcmp(str, "EXIT") == 0) return 0;
    if (strcmp(str, "HUP") == 0) return SIGHUP;
    if (strcmp(str, "INT") == 0) return SIGINT;
    if (strcmp(str, "QUIT") == 0) return SIGQUIT;
    if (strcmp(str, "KILL") == 0) return SIGKILL;
    if (strcmp(str, "TERM") == 0) return SIGTERM;
    if (strcmp(str, "CHLD") == 0) return SIGCHLD;
    if (strcmp(str, "STOP") == 0) return SIGSTOP;
    if (strcmp(str, "TSTP") == 0) return SIGTSTP;
    return -1;
}

void check_traps(void) {
    if (!trap_pending_any) return;
    trap_pending_any = 0;
    for (int i = 1; i < EXEC_SIG_MAX; i++) {
        if (pending_traps[i]) {
            pending_traps[i] = 0;
            if (trap_commands[i]) {
                execute_line(trap_commands[i]);
            }
        }
    }
}

// Function entry
typedef struct function_entry {
    char *name;
    ast_node_t *body;
    struct function_entry *next;
} function_entry_t;

static function_entry_t *functions = NULL;

// Return Status
static int func_return_signaled = 0;
static int func_return_status = 0;

// Loop control
static int loop_break_count = 0;
static int loop_continue_count = 0;

// Forward declarations of handlers
static int execute_simple_command(ast_simple_command_t *cmd);
static int execute_function(function_entry_t *func, int argc, char **argv);
static int execute_function_def(ast_function_t *func_def);
static int handle_return(int argc, char **argv);
static int execute_pipeline(ast_pipeline_t *pipe);
static int execute_binary_op(ast_binary_op_t *bin);
static int execute_if(ast_if_t *if_node);
static int execute_while(ast_while_t *w);
static int execute_for(ast_for_t *f);
static int execute_for(ast_for_t *f);
static int execute_case(ast_case_t *c);
static int execute_subshell(ast_subshell_t *sub);
static int execute_subshell(ast_subshell_t *sub);
static int apply_redirections(ast_redirection_t *redir);
static char *find_in_path(const char *cmd);

typedef struct fd_save {
    int orig_fd;
    int saved_fd; // -1 if originally closed
    struct fd_save *next;
} fd_save_t;

static fd_save_t *save_redirections(ast_redirection_t *redir) {
    fd_save_t *head = NULL;
    ast_redirection_t *r = redir;
    while (r) {
        fd_save_t *save = malloc(sizeof(fd_save_t));
        save->orig_fd = r->fd;
        save->saved_fd = dup(r->fd);
        if (save->saved_fd < 0 && errno == EBADF) {
            save->saved_fd = -1; // Was closed
        }
        
        save->next = head;
        head = save;
        
        r = r->next;
    }
    return head;
}

static void restore_redirections(fd_save_t *save) {
    while (save) {
        if (save->saved_fd != -1) {
            dup2(save->saved_fd, save->orig_fd);
            close(save->saved_fd);
        } else {
            close(save->orig_fd);
        }
        fd_save_t *next = save->next;
        free(save);
        save = next;
    }
}

int execute_ast(ast_node_t *node) {
    if (!node) return 0;

    int status = 0;
    fd_save_t *saved_fds = NULL;

    if (node->type != NODE_SIMPLE_COMMAND && node->type != NODE_SUBSHELL) {
        if (node->redirections) {
            saved_fds = save_redirections(node->redirections);
            if (apply_redirections(node->redirections) != 0) {
                 restore_redirections(saved_fds);
                 return 1;
            }
        }
    }

    switch (node->type) {
        case NODE_SIMPLE_COMMAND:
            // Simple command handles its own redirections (builtins vs external)
            // For now, external commands fork so we don't save.
            // Builtins currently might not redirect correctly without saving?
            // TODO: Fix builtin redirections in execute_simple_command
            status = execute_simple_command((ast_simple_command_t *)node);
            break;
        case NODE_PIPELINE:
            status = execute_pipeline((ast_pipeline_t *)node);
            break;
        case NODE_BINARY_OP: {
            ast_binary_op_t *bin = (ast_binary_op_t *)node;
            /* AND/OR lists handle errors internally - skip errexit check for their result */
            if (bin->op == OP_AND || bin->op == OP_OR) {
                status = execute_binary_op(bin);
                if (saved_fds) restore_redirections(saved_fds);
                return status;  /* Skip errexit check - AND/OR handles errors */
            } else {
                status = execute_binary_op(bin);
            }
            break;
        }
        case NODE_IF:
            status = execute_if((ast_if_t *)node);
            break;
        case NODE_WHILE:
            status = execute_while((ast_while_t *)node);
            break;
        case NODE_FOR:
            status = execute_for((ast_for_t *)node);
            break;
        case NODE_CASE:
            status = execute_case((ast_case_t *)node);
            break;
        case NODE_FUNCTION:
            status = execute_function_def((ast_function_t *)node);
            break;
        case NODE_SUBSHELL:
            // Subshell forks, so we can just apply redirections in the child handler if needed,
            // or here? execute_subshell likely forks.
            // Let's modify execute_subshell to apply redirections after fork.
            status = execute_subshell((ast_subshell_t *)node);
            break;
        default:
            fprintf(stderr, "%s: Unknown node type %d\n", shell_var_get_name(), node->type);
            status = 1;
    }

    if (status != 0 && shell_errexit && errexit_disabled == 0) {
        exit(status);
    }

    if (saved_fds) {
        restore_redirections(saved_fds);
    }
    return status;
}

static int handle_builtin(int argc, char **argv, ast_simple_command_t *cmd_node) {
    if (strcmp(argv[0], "exit") == 0) {
        int status = 0;
        if (argc > 1) status = atoi(argv[1]);
        exit(status);
    }
    if (strcmp(argv[0], "cd") == 0) {
        char *path = argv[1];
        if (!path) path = shell_var_get("HOME");
        if (!path) path = "/";
        if (chdir(path) < 0) {
            perror("cd");
            return 1;
        }
        return 0;
    }
    if (strcmp(argv[0], "export") == 0) {
        if (argc > 1) {
            char *eq = strchr(argv[1], '=');
            if (eq) {
                *eq = 0;
                shell_var_export(argv[1], eq + 1);
            } else {
                shell_var_export(argv[1], "");
            }
        }
        return 0;
    }
    if (strcmp(argv[0], "unset") == 0) {
        if (argc > 1) {
            shell_var_unset(argv[1]);
        }
        return 0;
    }
    if (strcmp(argv[0], "shift") == 0) {
        int n = 1;
        if (argc > 1) {
            n = atoi(argv[1]);
        }
        shell_var_shift(n);
        return 0;
    }
    if (strcmp(argv[0], "set") == 0) {
        if (argc > 1) {
            for (int i = 1; i < argc; i++) {
                if (argv[i][0] == '-') {
                    for (char *p = argv[i] + 1; *p; p++) {
                        if (*p == 'e') shell_errexit = 1;
                    }
                } else if (argv[i][0] == '+') {
                    for (char *p = argv[i] + 1; *p; p++) {
                        if (*p == 'e') shell_errexit = 0;
                    }
                } else if (strcmp(argv[i], "--") == 0) {
                    shell_var_set_args(argc - i - 1, argv + i + 1);
                    break;
                }
            }
        }
        return 0;
    }
    if (strcmp(argv[0], "exec") == 0) {
        /* exec [-a name] [command [args...]]
         * Replace shell with command. If no command, just apply redirections. */
        if (apply_redirections(cmd_node->base.redirections) != 0) return 1;

        if (argc == 1) return 0;  /* No command - redirections applied */

        int arg_idx = 1;
        char *argv0_override = NULL;

        /* Handle -a option for argv[0] override */
        if (argc > 2 && strcmp(argv[1], "-a") == 0) {
            argv0_override = argv[2];
            arg_idx = 3;
            if (arg_idx >= argc) {
                fprintf(stderr, "%s: exec: -a requires command\n", shell_var_get_name());
                return 1;
            }
        }

        char *full_path = find_in_path(argv[arg_idx]);
        if (!full_path) {
            fprintf(stderr, "%s: exec: %s: command not found\n", shell_var_get_name(), argv[arg_idx]);
            exit(127);  /* POSIX: exec failure exits the shell */
        }

        /* Build argv for execve */
        char **exec_argv = argv + arg_idx;
        char *saved_argv0 = NULL;
        if (argv0_override) {
            saved_argv0 = exec_argv[0];
            exec_argv[0] = argv0_override;
        }

        char **envp = shell_var_get_envp();
        execve(full_path, exec_argv, envp);

        /* execve failed - restore argv[0] for error message */
        if (saved_argv0) exec_argv[0] = saved_argv0;
        perror(argv[arg_idx]);
        free(full_path);
        exit(126);
    }
    if (strcmp(argv[0], "eval") == 0) {
        if (argc > 1) {
            int total_len = 0;
            for (int i = 1; i < argc; i++) total_len += strlen(argv[i]) + 1;
            char *line = malloc(total_len + 1);
            if (line) {
                line[0] = 0;
                for (int i = 1; i < argc; i++) {
                    strcat(line, argv[i]);
                    if (i < argc - 1) strcat(line, " ");
                }
                execute_line(line);
                free(line);
            }
        }
        return 0;
    }
    if (strcmp(argv[0], "wait") == 0) {
        int last_status = 0;
        if (argc > 1) {
            /* Wait for specific PIDs or job specs */
            for (int i = 1; i < argc; i++) {
                pid_t pid = -1;
                job_t *j = NULL;

                if (argv[i][0] == '%') {
                    /* Job specification: %n */
                    int jid = atoi(argv[i] + 1);
                    j = first_job;
                    while (j && j->id != jid) j = j->next;
                    if (!j) {
                        fprintf(stderr, "%s: wait: %s: no such job\n", shell_var_get_name(), argv[i]);
                        last_status = 127;
                        continue;
                    }
                } else {
                    pid = atoi(argv[i]);
                    /* Check if pid belongs to a tracked job */
                    job_t *it = first_job;
                    while (it) {
                        process_t *p = it->first_process;
                        while (p) {
                            if (p->pid == pid) { j = it; break; }
                            p = p->next;
                        }
                        if (j) break;
                        it = it->next;
                    }
                }

                if (j) {
                    job_wait(j);
                    /* Get status from last process */
                    process_t *p = j->first_process;
                    while (p && p->next) p = p->next;
                    if (p && WIFEXITED(p->status)) {
                        last_status = WEXITSTATUS(p->status);
                    } else if (p && WIFSIGNALED(p->status)) {
                        last_status = 128 + WTERMSIG(p->status);
                    }
                    job_update_status();
                } else if (pid > 0) {
                    int wstatus;
                    if (waitpid(pid, &wstatus, 0) < 0) {
                        /* Not our child or already reaped */
                        last_status = 127;
                    } else {
                        if (WIFEXITED(wstatus)) last_status = WEXITSTATUS(wstatus);
                        else if (WIFSIGNALED(wstatus)) last_status = 128 + WTERMSIG(wstatus);
                    }
                }
            }
        } else {
            /* Wait for all background jobs */
            while (first_job) {
                job_wait(first_job);
                job_update_status();
            }
            /* Reap any remaining zombies */
            while (waitpid(-1, NULL, WNOHANG) > 0);
        }
        return last_status;
    }
    if (strcmp(argv[0], "kill") == 0) {
        if (argc < 2) { fprintf(stderr, "kill: usage: kill [-sig] pid\n"); return 1; }
        int sig = SIGTERM;
        int idx = 1;
        if (argv[1][0] == '-') { sig = atoi(argv[1]+1); idx = 2; }
        if (idx >= argc) return 1;
        if (kill(atoi(argv[idx]), sig) < 0) { perror("kill"); return 1; }
        return 0;
    }
    if (strcmp(argv[0], "jobs") == 0) return builtin_jobs(argc, argv);
    if (strcmp(argv[0], "fg") == 0) return builtin_fg(argc, argv);
    if (strcmp(argv[0], "bg") == 0) return builtin_bg(argc, argv);
    if (strcmp(argv[0], "return") == 0) return handle_return(argc, argv);
    if (strcmp(argv[0], "local") == 0) {
        if (argc > 1) {
            char *eq = strchr(argv[1], '=');
            if (eq) {
                *eq = 0;
                shell_var_set_local(argv[1], eq + 1);
                *eq = '=';
            } else {
                shell_var_set_local(argv[1], "");
            }
        }
        return 0;
    }
    if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            printf("%s%s", argv[i], (i == argc - 1) ? "" : " ");
        }
        printf("\n");
        fflush(stdout);
        return 0;
    }
    
    if (strcmp(argv[0], "[") == 0 || strcmp(argv[0], "test") == 0) {
        if (argc < 2) return 1; 
        
        int is_bracket = (argv[0][0] == '[');
        int real_argc = argc;
        if (is_bracket) {
            if (strcmp(argv[argc-1], "]") != 0) {
                fprintf(stderr, "%s: [: missing `]'\n", shell_var_get_name());
                return 2;
            }
            real_argc--;
        }
        
        if (real_argc == 2) return (argv[1][0] == '\0');
        
        if (real_argc == 4) {
            char *left = argv[1];
            char *op = argv[2];
            char *right = argv[3];
            
            if (strcmp(op, "=") == 0) return strcmp(left, right) != 0;
            if (strcmp(op, "!=") == 0) return strcmp(left, right) == 0;
            if (strcmp(op, "-lt") == 0) return (atoi(left) < atoi(right)) ? 0 : 1;
            if (strcmp(op, "-gt") == 0) return (atoi(left) > atoi(right)) ? 0 : 1;
            if (strcmp(op, "-eq") == 0) return (atoi(left) == atoi(right)) ? 0 : 1;
            if (strcmp(op, "-ne") == 0) return (atoi(left) != atoi(right)) ? 0 : 1;
        }
        return 1;
    }
    
    // : (null command) - always succeeds
    if (strcmp(argv[0], ":") == 0) {
        return 0;
    }
    
    // . (dot/source) - execute commands from file
    if (strcmp(argv[0], ".") == 0 || strcmp(argv[0], "source") == 0) {
        if (argc < 2) {
            fprintf(stderr, "%s: %s: filename argument required\n", shell_var_get_name(), argv[0]);
            return 2;
        }
        FILE *f = fopen(argv[1], "r");
        if (!f) {
            fprintf(stderr, "%s: %s: %s: No such file or directory\n", shell_var_get_name(), argv[0], argv[1]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (fsize > 0) {
            char *content = malloc(fsize + 1);
            if (content) {
                size_t n = fread(content, 1, fsize, f);
                content[n] = 0;
                execute_line(content);
                free(content);
            }
        }
        fclose(f);
        return 0;
    }
    
    // break [n]
    if (strcmp(argv[0], "break") == 0) {
        loop_break_count = 1;
        if (argc > 1) {
            int n = atoi(argv[1]);
            if (n > 0) loop_break_count = n;
        }
        return 0;
    }
    
    // continue [n]
    if (strcmp(argv[0], "continue") == 0) {
        loop_continue_count = 1;
        if (argc > 1) {
            int n = atoi(argv[1]);
            if (n > 0) loop_continue_count = n;
        }
        return 0;
    }

    /* times - print accumulated user and system times */
    if (strcmp(argv[0], "times") == 0) {
        struct tms t;
        if (times(&t) == (clock_t)-1) {
            perror("times");
            return 1;
        }
        long clk_tck;
#ifdef _SC_CLK_TCK
        clk_tck = sysconf(_SC_CLK_TCK);
#elif defined(CLK_TCK)
        clk_tck = CLK_TCK;
#else
        clk_tck = 100;
#endif
        /* Format: XmY.ZZs for shell user/sys, then children user/sys */
        long ticks, secs, frac, mins;

        ticks = t.tms_utime;
        secs = ticks / clk_tck; frac = (ticks % clk_tck) * 100 / clk_tck;
        mins = secs / 60; secs %= 60;
        printf("%ldm%ld.%02lds ", mins, secs, frac);

        ticks = t.tms_stime;
        secs = ticks / clk_tck; frac = (ticks % clk_tck) * 100 / clk_tck;
        mins = secs / 60; secs %= 60;
        printf("%ldm%ld.%02lds\n", mins, secs, frac);

        ticks = t.tms_cutime;
        secs = ticks / clk_tck; frac = (ticks % clk_tck) * 100 / clk_tck;
        mins = secs / 60; secs %= 60;
        printf("%ldm%ld.%02lds ", mins, secs, frac);

        ticks = t.tms_cstime;
        secs = ticks / clk_tck; frac = (ticks % clk_tck) * 100 / clk_tck;
        mins = secs / 60; secs %= 60;
        printf("%ldm%ld.%02lds\n", mins, secs, frac);
        return 0;
    }

    /* umask [mode] - display or set file creation mask */
    if (strcmp(argv[0], "umask") == 0) {
        if (argc > 1) {
            char *end;
            long mask = strtol(argv[1], &end, 8);
            if (*end) {
                fprintf(stderr, "%s: umask: %s: invalid octal number\n", shell_var_get_name(), argv[1]);
                return 1;
            }
            umask((mode_t)mask);
        } else {
            mode_t cur = umask(0);
            umask(cur);
            printf("%04o\n", (unsigned)cur);
        }
        return 0;
    }

    /* trap [action] [signal...] - manage signal handlers */
    if (strcmp(argv[0], "trap") == 0) {
        if (argc == 1) {
            /* List defined traps */
            for (int i = 0; i < EXEC_SIG_MAX; i++) {
                if (trap_commands[i]) {
                    printf("trap -- '%s' %d\n", trap_commands[i], i);
                }
            }
            return 0;
        }
        /* trap action sig... OR trap - sig... */
        const char *action = argv[1];
        int start_idx = 2;
        int reset = (strcmp(action, "-") == 0);

        if (argc == 2 && !reset) {
            /* Single arg: might be signal to reset */
            int sig = parse_signal(action);
            if (sig >= 0 && sig < EXEC_SIG_MAX) {
                if (trap_commands[sig]) { free(trap_commands[sig]); trap_commands[sig] = NULL; }
                if (sig > 0) signal(sig, SIG_DFL);
                return 0;
            }
        }

        for (int i = start_idx; i < argc; i++) {
            int sig = parse_signal(argv[i]);
            if (sig < 0 || sig >= EXEC_SIG_MAX) {
                fprintf(stderr, "%s: trap: %s: invalid signal specification\n", shell_var_get_name(), argv[i]);
                continue;
            }
            if (trap_commands[sig]) { free(trap_commands[sig]); trap_commands[sig] = NULL; }
            if (reset) {
                if (sig > 0) signal(sig, SIG_DFL);
            } else {
                trap_commands[sig] = strdup(action);
                if (sig > 0) signal(sig, trap_handler);
            }
        }
        return 0;
    }

    /* command [-pVv] name [args...] - run command, bypassing functions */
    if (strcmp(argv[0], "command") == 0) {
        int opt_p = 0, opt_v = 0, opt_V = 0;
        int arg_idx = 1;

        while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1]) {
            for (char *p = argv[arg_idx] + 1; *p; p++) {
                switch (*p) {
                    case 'p': opt_p = 1; break;
                    case 'v': opt_v = 1; break;
                    case 'V': opt_V = 1; break;
                    default:
                        fprintf(stderr, "%s: command: -%c: invalid option\n", shell_var_get_name(), *p);
                        return 2;
                }
            }
            arg_idx++;
        }
        if (arg_idx >= argc) return 0;  /* No command specified */

        const char *cmd_name = argv[arg_idx];

        if (opt_v || opt_V) {
            /* Identify command type */
            /* Check functions */
            function_entry_t *func = functions;
            while (func) {
                if (strcmp(func->name, cmd_name) == 0) {
                    if (opt_v) printf("%s\n", cmd_name);
                    else printf("%s is a function\n", cmd_name);
                    return 0;
                }
                func = func->next;
            }
            /* Check builtins */
            if (is_builtin(cmd_name)) {
                if (opt_v) printf("%s\n", cmd_name);
                else printf("%s is a shell builtin\n", cmd_name);
                return 0;
            }
            /* Check PATH */
            char *saved_path = NULL;
            if (opt_p) {
                char *cur = shell_var_get("PATH");
                saved_path = cur ? strdup(cur) : strdup("");
                shell_var_export("PATH", "/bin:/usr/bin");
            }
            char *full = find_in_path(cmd_name);
            if (opt_p) {
                shell_var_export("PATH", saved_path);
                free(saved_path);
            }
            if (full) {
                if (opt_v) printf("%s\n", full);
                else printf("%s is %s\n", cmd_name, full);
                free(full);
                return 0;
            }
            if (opt_V) fprintf(stderr, "%s: %s: not found\n", shell_var_get_name(), cmd_name);
            return 127;
        }

        /* Execute command, bypassing functions */
        char *saved_path = NULL;
        if (opt_p) {
            char *cur = shell_var_get("PATH");
            saved_path = cur ? strdup(cur) : strdup("");
            shell_var_export("PATH", "/bin:/usr/bin");
        }

        /* Build new argv for the command */
        int new_argc = argc - arg_idx;
        char **new_argv = malloc((new_argc + 1) * sizeof(char*));
        for (int i = 0; i < new_argc; i++) new_argv[i] = argv[arg_idx + i];
        new_argv[new_argc] = NULL;

        /* Try builtins first */
        int status = handle_builtin(new_argc, new_argv, cmd_node);
        if (status == -1) {
            /* External command */
            char *full = find_in_path(new_argv[0]);
            if (!full) {
                fprintf(stderr, "%s: %s: command not found\n", shell_var_get_name(), new_argv[0]);
                status = 127;
            } else {
                pid_t pid = fork();
                if (pid == 0) {
                    signal(SIGINT, SIG_DFL);
                    signal(SIGQUIT, SIG_DFL);
                    char **envp = shell_var_get_envp();
                    execve(full, new_argv, envp);
                    perror(new_argv[0]);
                    _exit(126);
                } else if (pid > 0) {
                    int wstatus;
                    waitpid(pid, &wstatus, 0);
                    if (WIFEXITED(wstatus)) status = WEXITSTATUS(wstatus);
                    else if (WIFSIGNALED(wstatus)) status = 128 + WTERMSIG(wstatus);
                    else status = 1;
                } else {
                    perror("fork");
                    status = 1;
                }
                free(full);
            }
        }

        free(new_argv);
        if (opt_p) {
            shell_var_export("PATH", saved_path);
            free(saved_path);
        }
        return status;
    }

    /* read [-r] var... - read line from stdin into variables */
    if (strcmp(argv[0], "read") == 0) {
        int opt_r = 0, start = 1;
        if (argc > 1 && strcmp(argv[1], "-r") == 0) { opt_r = 1; start = 2; }
        if (start >= argc) {
            fprintf(stderr, "%s: read: missing variable name\n", shell_var_get_name());
            return 1;
        }

        char line[4096];
        if (!fgets(line, sizeof(line), stdin)) return 1;  /* EOF */

        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';

        /* Handle backslash continuation unless -r */
        if (!opt_r) {
            while (len > 0 && line[len-1] == '\\') {
                line[--len] = '\0';
                if (!fgets(line + len, sizeof(line) - len, stdin)) break;
                len = strlen(line);
                if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
            }
        }

        /* Split by IFS and assign to variables */
        char *ifs = shell_var_get("IFS");
        if (!ifs) ifs = " \t\n";

        char *saveptr = NULL;
        char *token = strtok_r(line, ifs, &saveptr);
        for (int i = start; i < argc; i++) {
            if (i == argc - 1) {
                /* Last variable gets rest of line */
                if (token) {
                    char *rest = token;
                    if (saveptr && *saveptr) {
                        /* Reconstruct remaining with original spacing */
                        size_t rest_len = strlen(token) + 1 + strlen(saveptr);
                        char *buf = malloc(rest_len + 1);
                        snprintf(buf, rest_len + 1, "%s%c%s", token, *ifs, saveptr);
                        shell_var_set(argv[i], buf);
                        free(buf);
                    } else {
                        shell_var_set(argv[i], rest);
                    }
                } else {
                    shell_var_set(argv[i], "");
                }
            } else {
                shell_var_set(argv[i], token ? token : "");
                token = strtok_r(NULL, ifs, &saveptr);
            }
        }
        return 0;
    }

    /* readonly [name[=value]...] - make variables readonly */
    if (strcmp(argv[0], "readonly") == 0) {
        if (argc == 1) {
            shell_var_print();
            return 0;
        }
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                shell_var_set(argv[i], eq + 1);
                *eq = '=';
            }
            /* Mark as readonly - would need shell_var API extension */
        }
        return 0;
    }

    /* getopts optstring name [args...] - POSIX option parsing */
    if (strcmp(argv[0], "getopts") == 0) {
        if (argc < 3) {
            fprintf(stderr, "%s: getopts: usage: getopts optstring name [args]\n", shell_var_get_name());
            return 2;
        }

        const char *optstring = argv[1];
        const char *varname = argv[2];

        /* Get current OPTIND (1-based index into positional params) */
        char *optind_str = shell_var_get("OPTIND");
        int optind = optind_str ? atoi(optind_str) : 1;
        if (optind < 1) optind = 1;

        /* Determine argument list: use remaining argv or positional params */
        char **args;
        int arg_count;
        if (argc > 3) {
            /* Explicit args provided */
            args = argv + 3;
            arg_count = argc - 3;
        } else {
            /* Use positional parameters $1, $2, ... */
            args = shell_var_get_positional(&arg_count);
        }

        /* Check if we've exhausted arguments */
        if (optind > arg_count) {
            shell_var_set(varname, "?");
            return 1;
        }

        const char *current_arg = args[optind - 1];

        /* Check if current arg is an option */
        if (current_arg[0] != '-' || current_arg[1] == '\0') {
            /* Not an option - end of options */
            shell_var_set(varname, "?");
            return 1;
        }
        if (strcmp(current_arg, "--") == 0) {
            /* End of options marker */
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", optind + 1);
            shell_var_set("OPTIND", buf);
            shell_var_set(varname, "?");
            return 1;
        }

        /* Get option character (OPTPOS tracks position within clustered opts) */
        char *optpos_str = shell_var_get("OPTPOS");
        int optpos = optpos_str ? atoi(optpos_str) : 1;
        if (optpos < 1) optpos = 1;

        char opt = current_arg[optpos];
        if (opt == '\0') {
            /* Move to next argument */
            optind++;
            optpos = 1;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", optind);
            shell_var_set("OPTIND", buf);
            shell_var_set("OPTPOS", "1");
            /* Retry with next argument (recursive call or re-invoke) */
            shell_var_set(varname, "?");
            return 1;
        }

        /* Find option in optstring */
        const char *found = strchr(optstring, opt);
        char optchar[2] = { opt, '\0' };

        if (!found || opt == ':') {
            /* Invalid option */
            if (optstring[0] != ':') {
                fprintf(stderr, "%s: illegal option -- %c\n", shell_var_get_name(), opt);
            }
            shell_var_set(varname, "?");
            shell_var_set("OPTARG", optchar);

            /* Advance position */
            if (current_arg[optpos + 1] == '\0') {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", optind + 1);
                shell_var_set("OPTIND", buf);
                shell_var_set("OPTPOS", "1");
            } else {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", optpos + 1);
                shell_var_set("OPTPOS", buf);
            }
            return 0;
        }

        /* Valid option found */
        shell_var_set(varname, optchar);

        /* Check if option requires an argument */
        if (found[1] == ':') {
            /* Option requires argument */
            if (current_arg[optpos + 1] != '\0') {
                /* Argument is rest of current arg */
                shell_var_set("OPTARG", current_arg + optpos + 1);
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", optind + 1);
                shell_var_set("OPTIND", buf);
                shell_var_set("OPTPOS", "1");
            } else if (optind < arg_count) {
                /* Argument is next arg */
                shell_var_set("OPTARG", args[optind]);
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", optind + 2);
                shell_var_set("OPTIND", buf);
                shell_var_set("OPTPOS", "1");
            } else {
                /* Missing argument */
                if (optstring[0] == ':') {
                    shell_var_set(varname, ":");
                    shell_var_set("OPTARG", optchar);
                } else {
                    fprintf(stderr, "%s: option requires an argument -- %c\n", shell_var_get_name(), opt);
                    shell_var_set(varname, "?");
                }
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", optind + 1);
                shell_var_set("OPTIND", buf);
                shell_var_set("OPTPOS", "1");
                return 0;
            }
        } else {
            /* No argument required */
            shell_var_unset("OPTARG");
            if (current_arg[optpos + 1] == '\0') {
                /* Move to next argument */
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", optind + 1);
                shell_var_set("OPTIND", buf);
                shell_var_set("OPTPOS", "1");
            } else {
                /* Stay on current arg, advance position */
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", optpos + 1);
                shell_var_set("OPTPOS", buf);
            }
        }

        return 0;
    }

    return -1; 
}

static int apply_redirections(ast_redirection_t *redir) {
    while (redir) {
        int fd;
        char *expanded = NULL;
        
        // Expand filename (except for here-docs which use heredoc_content)
        if (redir->type != REDIR_HERE_DOC && redir->filename) {
            expanded = expand_word(redir->filename);
            if (!expanded) {
                fprintf(stderr, "%s: expansion failed for redirection\n", shell_var_get_name());
                return 1;
            }
        }
        
        switch (redir->type) {
            case REDIR_OUT:
                fd = open(expanded, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror(expanded); free(expanded); return 1; }
                dup2(fd, redir->fd);
                close(fd);
                break;
            case REDIR_APPEND:
                fd = open(expanded, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd < 0) { perror(expanded); free(expanded); return 1; }
                dup2(fd, redir->fd);
                close(fd);
                break;
            case REDIR_IN:
                fd = open(expanded, O_RDONLY);
                if (fd < 0) { perror(expanded); free(expanded); return 1; }
                dup2(fd, redir->fd);
                close(fd);
                break;
            case REDIR_DUP_OUT: // >&
                if (strcmp(expanded, "-") == 0) {
                    close(redir->fd);
                } else {
                    int target = atoi(expanded);
                    if (dup2(target, redir->fd) < 0) { perror("dup2"); free(expanded); return 1; }
                }
                break;
            case REDIR_DUP_IN: // <&
                if (strcmp(expanded, "-") == 0) {
                    close(redir->fd);
                } else {
                    int target = atoi(expanded);
                    if (dup2(target, redir->fd) < 0) { perror("dup2"); free(expanded); return 1; }
                }
                break;
            case REDIR_HERE_DOC: // <<
                {
                    int p[2];
                    if (pipe(p) < 0) { perror("pipe"); return 1; }
                    if (redir->heredoc_content) {
                        char *heredoc_expanded = expand_heredoc(redir->heredoc_content, redir->quoted);
                        if (heredoc_expanded) {
                            write(p[1], heredoc_expanded, strlen(heredoc_expanded));
                            free(heredoc_expanded);
                        }
                    }
                    close(p[1]);
                    if (dup2(p[0], redir->fd) < 0) { perror("dup2"); return 1; }
                    close(p[0]);
                }
                break;
            default:
                break;
        }
        
        if (expanded) free(expanded);
        redir = redir->next;
    }
    return 0;
}

static char *find_in_path(const char *cmd) {
    if (strchr(cmd, '/')) return strdup(cmd);

    char *path = shell_var_get("PATH");
    if (!path) path = "/bin:/usr/bin";

    char *path_copy = strdup(path);
    char *dir = strtok(path_copy, ":");
    while (dir) {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);
        if (access(full_path, X_OK) == 0) {
            free(path_copy);
            return strdup(full_path);
        }
        dir = strtok(NULL, ":");
    }
    free(path_copy);
    return NULL;
}

static int execute_simple_command(ast_simple_command_t *cmd) {
    if (cmd->arg_count == 0) {
        // Assignments only: e.g. "VAR=val"
        fd_save_t *saved_fds = NULL;
        if (cmd->base.redirections) {
            saved_fds = save_redirections(cmd->base.redirections);
            if (apply_redirections(cmd->base.redirections) != 0) {
                restore_redirections(saved_fds);
                return 1;
            }
        }

        for (int i = 0; i < cmd->assign_count; i++) {
             char *eq = strchr(cmd->assignments[i], '=');
             if (eq) {
                 *eq = 0;
                 char *val_expanded = expand_word(eq + 1); // Expand RHS
                 shell_var_export(cmd->assignments[i], val_expanded ? val_expanded : "");
                 if (val_expanded) free(val_expanded);
                 *eq = '=';
             }
        }
        
        if (saved_fds) restore_redirections(saved_fds);
        return 0;
    }

    char **argv = expand_list(cmd->args);
    int argc = 0;
    while (argv[argc]) argc++;

    fd_save_t *saved_fds = NULL;
    if (cmd->base.redirections) {
        saved_fds = save_redirections(cmd->base.redirections);
        if (apply_redirections(cmd->base.redirections) != 0) {
            restore_redirections(saved_fds);
            for (int i=0; argv[i]; i++) free(argv[i]);
            free(argv);
            return 1;
        }
    }

    int status = handle_builtin(argc, argv, cmd);
    
    if (saved_fds) restore_redirections(saved_fds);
    
    if (status != -1) {
        for (int i=0; argv[i]; i++) free(argv[i]);
        free(argv);
        return status;
    }

    // Check functions (before PATH search)
    function_entry_t *func = functions;
    while (func) {
        if (strcmp(func->name, argv[0]) == 0) {
             int ret = execute_function(func, argc, argv);
             for (int i=0; argv[i]; i++) free(argv[i]);
             free(argv);
             return ret;
        }
        func = func->next;
    }

    char *full_path = find_in_path(argv[0]);
    if (!full_path) {
        fprintf(stderr, "%s: %s: command not found\n", shell_var_get_name(), argv[0]);
        for (int i=0; argv[i]; i++) free(argv[i]);
        free(argv);
        return 127;
    }

    int pid = fork();
    if (pid == 0) {
        if (apply_redirections(cmd->base.redirections) != 0) exit(1);
        
        // Construct expanded assignments properly
        // Ideally we should merge with shell_var_get_envp()
        // But since we are in child, we can just setenv/putenv?
        // But we are using custom shell_var.
        // Let's create a temporary env array merge.
        
        char **base_env = shell_var_get_envp();
        int env_count = 0;
        while(base_env[env_count]) env_count++;
        
        // New array size: base + assign_count + 1
        char **new_env = malloc((env_count + cmd->assign_count + 1) * sizeof(char*));
        for (int i=0; i<env_count; i++) new_env[i] = base_env[i];
        
        int current_count = env_count;
        for (int i=0; i<cmd->assign_count; i++) {
             char *eq = strchr(cmd->assignments[i], '=');
             if (eq) {
                 *eq = 0;
                 char *val = expand_word(eq + 1);
                 *eq = '=';
                 
                 // Create "KEY=VAL" string
                 // Note: duplication handling is tricky (should overwrite existing key).
                 // For now, simple append (most execve implementations use first occurrence, or last? POSIX undefined?)
                 // Linux execve: uses all, but getenv uses first.
                 // Correct logic: we should override.
                 
                 // Simpler: use putenv/setenv in child if libc supports it, then use extern char **environ?
                 // But we are passing new_env to execve.
                 
                 // Let's just construct strings.
                 // To do it right, we should scan base_env for Key and replace.
                 
                 char buf[1024]; // Careful with size
                 snprintf(buf, sizeof(buf), "%s=%s", cmd->assignments[i], val ? val : "");
                 if (val) free(val);
                 
                 // Remove existing key from new_env if present
                 size_t key_len = eq - cmd->assignments[i];
                 int found = -1;
                 for (int k=0; k<current_count; k++) {
                     if (strncmp(new_env[k], cmd->assignments[i], key_len) == 0 && new_env[k][key_len] == '=') {
                         found = k;
                         break;
                     }
                 }
                 
                 char *entry = strdup(buf);
                 if (found != -1) {
                     new_env[found] = entry; // Override
                 } else {
                     new_env[current_count++] = entry;
                 }
                 *eq = '='; // Restore just in case
             }
        }
        new_env[current_count] = NULL;
        
        execve(full_path, argv, new_env);
        perror(argv[0]);
        exit(1);
    } else if (pid < 0) {
        perror("fork");
        free(full_path);
        return 1;
    }

    int wait_status;
    waitpid(pid, &wait_status, 0);

    free(full_path);
    for (int i=0; argv[i]; i++) free(argv[i]);
    free(argv);

    if (WIFEXITED(wait_status)) return WEXITSTATUS(wait_status);
    return 1;
}

static int execute_pipeline(ast_pipeline_t *pipe_node) {
    int n = pipe_node->command_count;
    int pipefds[2 * (n - 1)];

    // Create job if interactive
    job_t *job = NULL;
    if (shell_is_interactive) {
        job = job_new();
        job->command = ast_to_string((ast_node_t *)pipe_node);
    }

    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("pipe");
            return 1;
        }
    }

    pid_t pids[n];
    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // Child process
            if (shell_is_interactive) {
               pid_t pid = getpid();
               if (i == 0) {
                   setpgid(pid, pid);
               } else {
                   setpgid(pid, pids[0]);
               }
               if (i == 0) tcsetpgrp(STDIN_FILENO, pid);
               
               // Reset signal handlers to default
               signal(SIGINT, SIG_DFL);
               signal(SIGQUIT, SIG_DFL);
               signal(SIGTSTP, SIG_DFL);
               signal(SIGTTIN, SIG_DFL);
               signal(SIGTTOU, SIG_DFL);
               signal(SIGCHLD, SIG_DFL);
            }

            if (i > 0) {
                dup2(pipefds[(i - 1) * 2], 0);
            }
            if (i < n - 1) {
                dup2(pipefds[i * 2 + 1], 1);
            }
            for (int k = 0; k < 2 * (n - 1); k++) {
                close(pipefds[k]);
            }
            exit(execute_ast(pipe_node->commands[i]));
        } else if (pids[i] < 0) {
             perror("fork");
             return 1;
        }
        
        // Parent process
        if (shell_is_interactive && job) {
            if (i == 0) job->pgid = pids[0];
            setpgid(pids[i], job->pgid);
            // Add process to job
            // Need command name? Simple reconstruction or valid guess
            job_add_process(job, pids[i], NULL); 
        }
    }

    for (int i = 0; i < 2 * (n - 1); i++) {
        close(pipefds[i]);
    }

    if (shell_is_interactive && job) {
        tcsetpgrp(STDIN_FILENO, job->pgid);
        
        // Wait for job
        int status;
        waitpid(-job->pgid, &status, WUNTRACED);
        
        // Verify if stopped or exited
        if (WIFSTOPPED(status)) {
            printf("\n[%d]+ Stopped\t(job %d)\n", (int)job->pgid, (int)job->pgid);
            // Don't free job, keep it
        } else {
             // Mark completed
             // job_free(job);
        }
        
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    } else {
        // Non-interactive wait
        int last_status = 0;
        for (int i = 0; i < n; i++) {
            int status;
            waitpid(pids[i], &status, 0);
            if (i == n - 1 && WIFEXITED(status)) {
                last_status = WEXITSTATUS(status);
            }
        }
        return last_status;
    }

    return 0; // Return job status properly?
}

static int execute_binary_op(ast_binary_op_t *bin) {
    // Handle background execution specially
    if (bin->op == OP_BACKGROUND) {
        pid_t pid = fork();
        if (pid == 0) {
            // Child: run the command
            if (shell_is_interactive) {
                // Create new process group
                setpgid(0, 0);
                // Reset signal handlers
                signal(SIGINT, SIG_DFL);
                signal(SIGQUIT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGTTIN, SIG_DFL);
                signal(SIGTTOU, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);
            }
            exit(execute_ast(bin->left));
        } else if (pid > 0) {
            // Parent: add to job list
            if (shell_is_interactive) {
                setpgid(pid, pid);
                job_t *job = job_new();
                job->pgid = pid;
                job->command = ast_to_string(bin->left);
                job_add_process(job, pid, NULL);
                printf("[%d] %d\n", job->id, (int)pid);
            }
            // Set $! to PID of background process
            char pid_buf[16];
            snprintf(pid_buf, sizeof(pid_buf), "%d", (int)pid);
            shell_var_set("!", pid_buf);
            
            // Continue with right side if present (OP_BACKGROUND is unary, but just in case)
            if (bin->right) {
                return execute_ast(bin->right);
            }
            return 0;  // Background commands return 0 immediately
        } else {
            perror("fork");
            return 1;
        }
    }
    
    int left_status;
    if (bin->op == OP_AND || bin->op == OP_OR) {
        /* POSIX: errexit ignored for any command of an AND-OR list other than the last */
        errexit_disabled++;
        left_status = execute_ast(bin->left);
        errexit_disabled--;
    } else {
        left_status = execute_ast(bin->left);
    }
    
    // Update $? after each command so subsequent commands can see it
    char status_buf[16];
    snprintf(status_buf, sizeof(status_buf), "%d", left_status);
    shell_var_set("?", status_buf);
    
    if (func_return_signaled || loop_break_count > 0 || loop_continue_count > 0) return left_status;
    
    switch (bin->op) {
        case OP_SEQ:
            return execute_ast(bin->right);
        case OP_AND:
            if (left_status == 0) {
                return execute_ast(bin->right);  /* Right is "last" - errexit applies */
            }
            /* Short-circuit: left failed, don't execute right.
             * errexit is skipped by early return in execute_ast for AND nodes */
            return left_status;
        case OP_OR:
            if (left_status != 0) {
                return execute_ast(bin->right);  /* Right is "last" - errexit applies */
            }
            /* Short-circuit: left succeeded, return it */
            return left_status;
        case OP_BACKGROUND:
            break;
    }
    return 1;
}

static int execute_if(ast_if_t *if_node) {
    errexit_disabled++;
    int cond_status = execute_ast(if_node->condition);
    errexit_disabled--;
    
    if (func_return_signaled || loop_break_count > 0 || loop_continue_count > 0) return cond_status;
    
    if (cond_status == 0) {
        return execute_ast(if_node->then_body);
    } else {
        if (if_node->else_body)
            return execute_ast(if_node->else_body);
        return 0;
    }
}

static int execute_while(ast_while_t *w) {
    int status = 0;
    while (1) {
        errexit_disabled++;
        int cond_status = execute_ast(w->condition);
        errexit_disabled--;
        
        if (cond_status != 0) break;
        
        if (func_return_signaled) return status;
        status = execute_ast(w->body);
        if (func_return_signaled) return status;
        if (loop_break_count > 0) {
            loop_break_count--;
            break;
        }
        if (loop_continue_count > 0) {
            loop_continue_count--;
            if (loop_continue_count > 0) break; // continue to outer loop
            continue;
        }
    }
    return status;
}

static int execute_for(ast_for_t *f) {
    int status = 0;
    char **expanded = expand_list(f->elements);
    if (!expanded) return 0;
    
    for (int i = 0; expanded[i] != NULL; i++) {
        shell_var_set(f->var_name, expanded[i]);
        status = execute_ast(f->body);
        if (func_return_signaled) {
            for (int k = 0; expanded[k]; k++) free(expanded[k]);
            free(expanded);
            return status;
        }
        if (loop_break_count > 0) {
            loop_break_count--;
            break;
        }
        if (loop_continue_count > 0) {
            loop_continue_count--;
            if (loop_continue_count > 0) break; // continue to outer loop
            continue;
        }
    }
    
    for (int i = 0; expanded[i]; i++) free(expanded[i]);
    free(expanded);
    return status;
}

static int execute_case(ast_case_t *c) {
    char *word = expand_word(c->word);
    if (!word) word = strdup("");
    int status = 0;
    
    ast_case_item_t *item = c->items;
    while (item) {
        char *pattern = expand_word(item->pattern); 
        if (!pattern) pattern = strdup("");
        
        int match = 0;
        if (match_pattern(pattern, word)) {
             match = 1;
        }
        free(pattern);
        
        if (match) {
            status = execute_ast(item->body);
            break; 
        }
        item = item->next;
    }
    
    free(word);
    return status;
}

static int execute_subshell(ast_subshell_t *sub) {
    pid_t pid = fork();
    if (pid == 0) {
        exit(execute_ast(sub->list));
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

// Implement execute_function_def
static int execute_function_def(ast_function_t *func_def) {
    if (!func_def || !func_def->name) return 1;
    
    // Check if exists
    function_entry_t *curr = functions;
    while (curr) {
        if (strcmp(curr->name, func_def->name) == 0) {
            // Update
            if (curr->body) ast_free(curr->body);
            curr->body = func_def->body;
            // Detach payload from original AST node so it isn't freed by caller
            func_def->body = NULL; 
            return 0;
        }
        curr = curr->next;
    }
    
    // Add new
    function_entry_t *new_func = calloc(1, sizeof(function_entry_t));
    new_func->name = strdup(func_def->name);
    new_func->body = func_def->body;
    func_def->body = NULL; // Detach
    new_func->next = functions;
    functions = new_func;
    return 0;
}

static int execute_function(function_entry_t *func, int argc, char **argv) {
    shell_var_push_args();
    shell_var_push_scope();
    shell_var_set_args(argc, argv);
    
    // Reset return status
    int saved_return_signaled = func_return_signaled;
    int saved_return_status = func_return_status;
    func_return_signaled = 0;
    
    int status = execute_ast(func->body);
    
    if (func_return_signaled) {
        status = func_return_status;
        func_return_signaled = 0; // Handled
    }
    
    // Restore
    func_return_signaled = saved_return_signaled;
    func_return_status = saved_return_status;
    
    shell_var_pop_scope();
    shell_var_pop_args();
    return status;
}

// Add return builtin handler
static int handle_return(int argc, char **argv) {
    int ret = 0;
    if (argc > 1) {
        ret = atoi(argv[1]);
    }
    func_return_signaled = 1;
    func_return_status = ret;
    return ret;
}
