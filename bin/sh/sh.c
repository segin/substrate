#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include <sys/wait.h>
#include <signal.h>
#include "exec.h"
#include "expand.h"
#include "job.h"
#include "shell_var.h"
#include "util.h"
#include <termios.h>

static int command_count = 1;

extern int tcsetpgrp(int fd, pid_t pgrp);
extern pid_t tcgetpgrp(int fd);
extern pid_t getpgrp(void);
extern int setpgid(pid_t pid, pid_t pgid);
extern int tcgetattr(int fd, struct termios *termios_p);

#ifndef SIGTTIN
#define SIGTTIN 21
#endif
#ifndef SIGTTOU
#define SIGTTOU 22
#endif
#ifndef SIGCHLD
#define SIGCHLD 20
#endif
#ifndef SIGTSTP
#define SIGTSTP 18
#endif

int execute_line(char *buffer) {
    if (!buffer || !*buffer) return 0;
    
    lexer_t l;
    lexer_init(&l, buffer);
    int last_status = 0;
    
    while (1) {
        token_t *t = lexer_peek(&l);
        if (!t || t->type == TOKEN_EOF) break;
        
        ast_node_t *node = parser_parse(&l);
        if (node) {
            int status = execute_ast(node);
            last_status = status;
            char res_buf[16];
            snprintf(res_buf, sizeof(res_buf), "%d", status);
            shell_var_set("?", res_buf);
            ast_free(node);
        } else {
            // Parser error or empty
            break;
        }
        
        // Consume any trailing newlines or semicolons between commands
        while (1) {
            t = lexer_peek(&l);
            if (t && (t->type == TOKEN_NEWLINE || (t->type == TOKEN_OPERATOR && strcmp(t->value, ";") == 0))) {
                token_free(lexer_next(&l));
            } else {
                break;
            }
        }
    }
    return last_status;
}

// Source a file if it exists
static void source_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize > 0) {
        char *content = malloc(fsize + 1);
        if (content) {
            size_t n = fread(content, 1, fsize, f);
            content[n] = '\0';
            execute_line(content);
            free(content);
        }
    }
    fclose(f);
}

// Process startup files per POSIX
static void process_startup_files(int is_login_shell, int is_interactive) {
    // POSIX: Login shells read /etc/profile then ~/.profile
    if (is_login_shell) {
        source_file("/etc/profile");
        
        char *home = getenv("HOME");
        if (home) {
            char profile_path[1024];
            snprintf(profile_path, sizeof(profile_path), "%s/.profile", home);
            source_file(profile_path);
        }
    }
    
    // POSIX: Interactive shells read file specified by $ENV (after expansion)
    if (is_interactive) {
        char *env_file = getenv("ENV");
        if (env_file && env_file[0]) {
            char *expanded = expand_word(env_file);
            source_file(expanded ? expanded : env_file);
            if (expanded) free(expanded);
        }
    }
}

// Initialize default environment values
static void init_environment(void) {
    // POSIX: IFS default is space, tab, newline
    if (!shell_var_get("IFS")) {
        shell_var_set("IFS", " \t\n");
    }
    
    // Set PATH if not already set
    if (!getenv("PATH")) {
        shell_var_export("PATH", "/usr/bin:/bin");
    }
}

static void sigchld_handler(int sig) {
    (void)sig;
    // job_update_status will be called by the main loop.
}

// Helper to get last exit status
static int get_last_status(void) {
    char *s = shell_var_get("?");
    return s ? atoi(s) : 0;
}

static char *expand_prompt_escapes(const char *ps1) {
    if (!ps1) return NULL;
    size_t cap = strlen(ps1) * 2 + 16;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    
    const char *p = ps1;
    while (*p) {
        if (*p == '\\') {
            p++;
            if (!*p) {
                // Trailing backslash - append as is
                buffer_append(&buf, &cap, &len, '\\');
                break;
            }
            switch (*p) {
                case '!': {
                    char num[32];
                    snprintf(num, sizeof(num), "%d", command_count);
                    buffer_append_str(&buf, &cap, &len, num);
                    break;
                }
                case '$': {
                    char c = (geteuid() == 0) ? '#' : '$';
                    buffer_append(&buf, &cap, &len, c);
                    break;
                }
                case '\\':
                    // Literal backslash.
                    // IMPORTANT: To survive expand_word which removes quotes,
                    // we might need to output \\ if we want a single backslash?
                    // But expand_word treats \ as quote.
                    // If we output \, expand_word sees \ and escapes next char.
                    // If we want literal \, we should output \\.
                    buffer_append_str(&buf, &cap, &len, "\\\\");
                    break;
                default:
                    // Undefined escape: print literal char (e.g. \a -> a)
                    // But wait, user might expect \n to be newline?
                    // TASKS.md said "Handle undefined escapes deterministically (e.g., print literal char)".
                    // It didn't strictly require \n, \r etc.
                    // But standard prompt usually supports them.
                    // For now, I'll stick to printing the char literal.
                    buffer_append(&buf, &cap, &len, *p);
                    break;
            }
        } else {
            buffer_append(&buf, &cap, &len, *p);
        }
        p++;
    }
    buf[len] = 0;
    return buf;
}

static char *evaluate_prompt(const char *ps1) {
    if (!ps1) return NULL;
    
    // Save state
    int saved_xtrace = shell_xtrace;
    int saved_errexit = shell_errexit;
    char *saved_status_str = shell_var_get("?");
    char *saved_status = saved_status_str ? strdup(saved_status_str) : NULL;
    
    // Disable tracing and error exit for prompt evaluation
    shell_xtrace = 0;
    shell_errexit = 0;
    
    char *escaped = expand_prompt_escapes(ps1);
    char *expanded = expand_word(escaped ? escaped : ps1);
    if (escaped) free(escaped);
    
    // Restore state
    shell_xtrace = saved_xtrace;
    shell_errexit = saved_errexit;
    if (saved_status) {
        shell_var_set("?", saved_status);
        free(saved_status);
    } else {
        // If ? wasn't set (e.g. at start), it remains unset or we could unset it?
        // Usually ? is always set to 0 initially.
        // shell_var_get might return NULL?
        // If it was NULL, let's leave it as is or set to 0?
        // shell_var_set("?", "0") might be safer if it somehow got unset.
        // But for minimal intrusion, if it was NULL, we do nothing.
    }
    
    return expanded;
}


int main(int argc, char **argv, char **envp) {
    shell_var_init(envp);
    init_environment();
    
    // Shell flags
    int opt_c = 0;           // -c: Execute string
    int opt_s = 0;           // -s: Read from stdin
    int opt_i = 0;           // -i: Force interactive
    int is_login_shell = 0;  // argv[0] starts with '-'
    char *command_string = NULL;
    
    // Login shell detection
    if (argv[0] && argv[0][0] == '-') {
        is_login_shell = 1;
    }
    
    // POSIX option parsing
    int optind = 1;
    while (optind < argc && argv[optind][0] == '-' && argv[optind][1] != '\0') {
        char *arg = argv[optind];
        
        // "--" stops option processing
        if (strcmp(arg, "--") == 0) {
            optind++;
            break;
        }
        
        // Single "-" means read from stdin (not an option)
        if (strcmp(arg, "-") == 0) {
            break;
        }
        
        // Process option characters
        for (char *p = arg + 1; *p; p++) {
            switch (*p) {
                case 'c':
                    opt_c = 1;
                    break;
                case 's':
                    opt_s = 1;
                    break;
                case 'i':
                    opt_i = 1;
                    break;
                case 'e':
                    shell_errexit = 1;
                    break;
                case 'x':
                    shell_xtrace = 1;
                    break;
                default:
                    fprintf(stderr, "%s: illegal option -%c\n", shell_var_get_name(), *p);
                    return 2;
            }
        }
        optind++;
        
        // -c requires the next argument as the command string
        if (opt_c) {
            if (optind >= argc) {
                fprintf(stderr, "%s: -c: option requires an argument\n", shell_var_get_name());
                return 2;
            }
            command_string = argv[optind++];
            break;  // Stop option processing after -c command_string
        }
    }
    
    
    // Determine input source
    FILE *input = stdin;
    int reading_script = 0;
    
    if (!opt_s && optind < argc && strcmp(argv[optind], "-") != 0) {
        // Script file specified
        input = fopen(argv[optind], "r");
        if (!input) {
            perror(argv[optind]);
            return 127;
        }
        shell_var_set("0", argv[optind]);
        optind++;
        reading_script = 1;
    } else {
        // Read from stdin (-s or no script file)
        if (optind < argc && strcmp(argv[optind], "-") == 0) {
            optind++;  // Skip the "-"
        }
        shell_var_set("0", argv[0] ? argv[0] : "sh");
    }
    
    // Set positional parameters from remaining args
    shell_var_set_args(argc - optind, argv + optind);
    
    // Initialize Job Control for interactive shells
    int is_interactive = ((input == stdin && isatty(STDIN_FILENO)) || opt_i) && !opt_c;
    if (opt_i) is_interactive = 1;
    
    if (is_interactive && !reading_script && isatty(STDIN_FILENO)) {
        shell_is_interactive = 1;
        while (tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);
        
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGCHLD, sigchld_handler);
        
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("Couldn't put the shell in its own process group");
            exit(1);
        }
        
        tcsetpgrp(STDIN_FILENO, shell_pgid);
        tcgetattr(STDIN_FILENO, &shell_tmodes);
    }
    
    // Process startup files (profile for login, ENV for interactive)
    process_startup_files(is_login_shell, is_interactive && !reading_script);
    
    // Handle -c: Execute command string
    if (opt_c) {
        execute_line(command_string);
        run_exit_trap();
        return get_last_status();
    }
    
    if (reading_script || opt_s) {
        // Read entire input for non-interactive/script mode
        fseek(input, 0, SEEK_END);
        off_t fsize = ftell(input);
        fseek(input, 0, SEEK_SET);

        if (fsize < 0) {
            // stdin or pipe - read incrementally
            size_t cap = 4096, len = 0;
            char *buffer = malloc(cap);
            if (!buffer) {
                fprintf(stderr, "%s: Out of memory\n", shell_var_get_name());
                if (input != stdin) fclose(input);
                run_exit_trap();
                return 1;
            }
            
            int c;
            while ((c = fgetc(input)) != EOF) {
                if (len + 1 >= cap) {
                    cap *= 2;
                    buffer = realloc(buffer, cap);
                    if (!buffer) {
                        fprintf(stderr, "%s: Out of memory\n", shell_var_get_name());
                        run_exit_trap();
                        return 1;
                    }
                }
                buffer[len++] = c;
            }
            buffer[len] = 0;
            
            if (len > 0) {
                execute_line(buffer);
            }
            free(buffer);
        } else {
            char *buffer = malloc(fsize + 1);
            if (!buffer) {
                fprintf(stderr, "%s: Out of memory\n", shell_var_get_name());
                if (input != stdin) fclose(input);
                run_exit_trap();
                return 1;
            }
            size_t nread = fread(buffer, 1, (size_t)fsize, input);
            buffer[nread] = 0;
 
            if (nread > 0) {
                execute_line(buffer);
            } else if (fsize > 0) {
                fprintf(stderr, "%s: Failed to read script\n", shell_var_get_name());
            }
            free(buffer);
        }
        
        if (input != stdin) fclose(input);
    } else {
        // Interactive: line-by-line
        char buf[1024];
        while (1) {
            if (shell_is_interactive) {
                job_update_status();
                check_traps();
                char *p = shell_var_get("PS1");
                if (!p) p = "$ ";
                char *expanded = evaluate_prompt(p);
                printf("%s", expanded ? expanded : p);
                if (expanded) free(expanded);
                fflush(stdout);
            }
            
            if (!fgets(buf, sizeof(buf), input)) break;
            execute_line(buf);
            if (shell_is_interactive) command_count++;
            check_traps();
        }
    }
    run_exit_trap();
    return get_last_status();
}