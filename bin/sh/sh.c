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
#include <pwd.h>
#include "prompt.h"
#include <histedit.h>

static int command_count = 1;
int shell_promptvars = 1;   /* bash default: prompt performs $ expansion */

static EditLine *el = NULL;
static History *hist = NULL;
static const char *editline_prompt = "$ ";

static const char *editline_prompt_callback(EditLine *editline) {
    (void)editline;
    return editline_prompt ? editline_prompt : "$ ";
}

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
            int status = execute_ast(node, NULL);
            last_status = status;
            char res_buf[16];
            snprintf(res_buf, sizeof(res_buf), "%d", status);
            shell_var_set("?", res_buf);
            ast_free(node);
        } else {
            /* Parser error. `t` (peeked above) was consumed and freed by
             * parser_parse, so it must not be dereferenced here - and we
             * already know it was a non-EOF token (the loop broke on EOF at
             * the top), so this is unconditionally a parse error. */
            last_status = 2;
            shell_var_set("?", "2");
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
    char *content;

    if (!f) return;

    content = read_stream_all(f);
    if (content) {
        execute_line(content);
        free(content);
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
    char *ifs = shell_var_get("IFS");
    if (!ifs) {
        shell_var_set("IFS", " \t\n");
    }
    free(ifs);
    
    // Set PATH if not already set
    if (!getenv("PATH")) {
        shell_var_export("PATH", "/usr/bin:/bin:/usr/local/bin");
    }
    
    // Initialize Prompt Mode
    shell_var_export("SHELL_PROMPT_MODE", "POSIX");
    shell_var_set_readonly("SHELL_PROMPT_MODE");
}

// Helper to get last exit status
static int get_last_status(void) {
    char *s = shell_var_get("?");
    int status = s ? atoi(s) : 0;
    free(s);
    return status;
}




int main(int argc, char **argv, char **envp) {
    shell_var_init(envp);
    init_environment();
    shell_var_set_name((argv[0] && argv[0][0]) ? argv[0] : "sh");
    
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
                case 'l':
                    /* `-l`: login shell.  Equivalent semantically to
                     * argv[0] beginning with '-' — bin/login and
                     * bin/su both pass this when invoking a login
                     * shell, and POSIX/bash/dash accept it. */
                    is_login_shell = 1;
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
        shell_var_set_name(argv[optind]);
        optind++;
        reading_script = 1;
    } else {
        // Read from stdin (-s or no script file)
        if (optind < argc && strcmp(argv[optind], "-") == 0) {
            optind++;  // Skip the "-"
        }
        shell_var_set_name((argv[0] && argv[0][0]) ? argv[0] : "sh");
    }
    
    /* argv[optind-1] is the last consumed argument (program name, script name,
     * or command string). Pass it as the pseudo-argv[0] so shell_var_set_args
     * (which always skips argv[0]) correctly maps argv[optind] → $1. */
    shell_var_set_args(argc - optind + 1, argv + optind - 1);
    
    // Initialize Job Control for interactive shells
    int is_interactive = ((input == stdin && isatty(STDIN_FILENO)) || opt_i) && !opt_c;
    if (opt_i) is_interactive = 1;
    
    if (is_interactive && !reading_script) {
        shell_is_interactive = 1;
        
        if (isatty(STDIN_FILENO)) {
            while (tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp()))
                kill(-shell_pgid, SIGTTIN);
            
            signal(SIGINT, SIG_IGN);
            signal(SIGQUIT, SIG_IGN);
            signal(SIGTSTP, SIG_IGN);
            signal(SIGTTIN, SIG_IGN);
            signal(SIGTTOU, SIG_IGN);
            
            shell_pgid = getpid();
            if (setpgid(shell_pgid, shell_pgid) < 0) {
                perror("Couldn't put the shell in its own process group");
                exit(1);
            }
            
            tcsetpgrp(STDIN_FILENO, shell_pgid);
            tcgetattr(STDIN_FILENO, &shell_tmodes);
        }

        el = el_init("sh", input, stdout, stderr);
        hist = history_init();
        if (el && hist) {
            el_set(el, EL_HIST, history, hist);
            el_set(el, EL_EDITOR, "emacs");
            el_set(el, EL_PROMPT, editline_prompt_callback);
        }
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
            
            size_t nread;
            while ((nread = fread(buffer + len, 1, cap - len - 1, input)) > 0) {
                len += nread;
                if (cap - len < 4096) {
                    cap = len + 4096;
                    char *new_buffer = realloc(buffer, cap);
                    if (!new_buffer) {
                        fprintf(stderr, "%s: Out of memory\n", shell_var_get_name());
                        free(buffer);
                        run_exit_trap();
                        return 1;
                    }
                    buffer = new_buffer;
                }
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
        while (1) {
            char *line = NULL;
            int count = 0;
            /* buf must outlive the fgets block below: `line` aliases it and is
             * used (execute_line) after that block. Declaring it inside the
             * `if (!line)` block was a stack-use-after-scope. */
            char buf[1024];

            if (shell_is_interactive) {
                job_update_status();
                check_traps();
                char *p = shell_var_get("prompt");
                int extended = 1;  // prompt uses zsh-style %escapes
                if (!p) {
                    p = shell_var_get("PS1");
                    extended = 0;  // PS1 uses bash-style \escapes
                }
                int p_alloc = (p != NULL);
                if (!p) p = "$ ";
                char *expanded = evaluate_prompt(p, command_count, extended);
                
                if (el) {
                    editline_prompt = expanded ? expanded : p;
                    line = (char *)el_gets(el, &count);
                    editline_prompt = "$ ";
                    if (!line) {
                        if (expanded) free(expanded);
                        if (p_alloc) free(p);
                        break;
                    }
                } else {
                    printf("%s", expanded ? expanded : p);
                    fflush(stdout);
                }
                
                if (expanded) free(expanded);
                if (p_alloc) free(p);
            }
            
            if (!line) {
                if (!fgets(buf, sizeof(buf), input)) break;
                line = buf;
            }

            if (*line) {
                if (shell_is_interactive && hist) {
                    HistEvent ev;
                    history(hist, &ev, H_ENTER, line);
                }
                execute_line(line);
                if (shell_is_interactive) command_count++;
            }
            
            check_traps();
            line = NULL;
        }
    }

    if (el) el_end(el);
    if (hist) history_end(hist);

    run_exit_trap();
    return get_last_status();
}
