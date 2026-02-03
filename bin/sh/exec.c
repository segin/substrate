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
#include "job.h"
#include "util.h"

int shell_is_interactive = 0;
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
        case NODE_BINARY_OP:
            status = execute_binary_op((ast_binary_op_t *)node);
            break;
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
            fprintf(stderr, "sh: Unknown node type %d\n", node->type);
            status = 1;
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
        if (argc > 1 && strcmp(argv[1], "--") == 0) {
             shell_var_set_args(argc - 1, argv + 1);
        } else {
             shell_var_set_args(argc, argv);
        }
        return 0;
    }
    if (strcmp(argv[0], "exec") == 0) {
        if (apply_redirections(cmd_node->base.redirections) != 0) return 1;
        if (argc > 1) {
            char *full_path = find_in_path(argv[1]);
            if (!full_path) {
                fprintf(stderr, "sh: %s: command not found\n", argv[1]);
                return 127;
            }
            char **envp = shell_var_get_envp();
            execve(full_path, argv + 1, envp);
            perror(argv[1]);
            exit(126);
        }
        return 0;
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
                fprintf(stderr, "sh: [: missing `]'\n");
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
                fprintf(stderr, "sh: expansion failed for redirection\n");
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
        fprintf(stderr, "sh: %s: command not found\n", argv[0]);
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
    int left_status = execute_ast(bin->left);
    if (func_return_signaled) return left_status;
    
    switch (bin->op) {
        case OP_SEQ:
            return execute_ast(bin->right);
        case OP_AND:
            if (left_status == 0) return execute_ast(bin->right);
            return left_status;
        case OP_OR:
            if (left_status != 0) return execute_ast(bin->right);
            return left_status;
    }
    return 1;
}

static int execute_if(ast_if_t *if_node) {
    int cond_status = execute_ast(if_node->condition);
    if (func_return_signaled) return cond_status;
    
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
    while (execute_ast(w->condition) == 0) {
        if (func_return_signaled) return status;
        status = execute_ast(w->body);
        if (func_return_signaled) return status;
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
