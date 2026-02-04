#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include <sys/wait.h>
#include <signal.h>
#include "exec.h"
#include "job.h"
#include "shell_var.h"
#include <termios.h>

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

void execute_line(const char *buffer) {
    if (!buffer || !*buffer) return;
    
    lexer_t l;
    lexer_init(&l, buffer);
    
    while (1) {
        token_t *t = lexer_peek(&l);
        if (!t || t->type == TOKEN_EOF) break;
        
        ast_node_t *node = parser_parse(&l);
        if (node) {
            int status = execute_ast(node);
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
            // TODO: Expand $ENV value (for now, use as-is)
            source_file(env_file);
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
                default:
                    fprintf(stderr, "sh: illegal option -%c\n", *p);
                    return 2;
            }
        }
        optind++;
        
        // -c requires the next argument as the command string
        if (opt_c) {
            if (optind >= argc) {
                fprintf(stderr, "sh: -c: option requires an argument\n");
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
    int is_interactive = (input == stdin && isatty(STDIN_FILENO)) || opt_i;
    
    if (is_interactive && !reading_script) {
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
        return 0;
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
                fprintf(stderr, "sh: Out of memory\n");
                if (input != stdin) fclose(input);
                return 1;
            }
            
            int c;
            while ((c = fgetc(input)) != EOF) {
                if (len + 1 >= cap) {
                    cap *= 2;
                    buffer = realloc(buffer, cap);
                    if (!buffer) {
                        fprintf(stderr, "sh: Out of memory\n");
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
                fprintf(stderr, "sh: Out of memory\n");
                if (input != stdin) fclose(input);
                return 1;
            }
            size_t nread = fread(buffer, 1, (size_t)fsize, input);
            buffer[nread] = 0;
 
            if (nread > 0) {
                execute_line(buffer);
            } else if (fsize > 0) {
                fprintf(stderr, "sh: Failed to read script\n");
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
                char *p = shell_var_get("PS1");
                if (!p) p = "$ ";
                printf("%s", p);
                fflush(stdout);
            }
            
            if (!fgets(buf, sizeof(buf), input)) break;
            execute_line(buf);
        }
    }
    return 0;
}