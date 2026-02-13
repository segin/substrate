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
        int status = 0;
        if (argc > 1) {
            int total_len = 0;
            for (int i = 1; i < argc; i++) total_len += strlen(argv[i]) + 1;
            char *line = malloc(total_len + 1);
            if (line) {
                char *ptr = line;
                for (int i = 1; i < argc; i++) {
                    char *arg = argv[i];
                    while (*arg) *ptr++ = *arg++;
                    if (i < argc - 1) *ptr++ = ' ';
                }
                *ptr = '\0';
                status = execute_line(line);
                free(line);
            }
        }
        return status;
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
        if (argv[1][0] == '-') { 
            sig = parse_signal(argv[1]+1);
            if (sig < 0) {
                fprintf(stderr, "kill: %s: invalid signal specification\n", argv[1]+1);
                return 1;
            }
            idx = 2;
        }
        if (idx >= argc) return 1;
        if (kill(atoi(argv[idx]), sig) < 0) { perror("kill"); return 1; }
        return 0;
    }
    if (strcmp(argv[0], "jobs") == 0) return builtin_jobs(argc, argv);
    if (strcmp(argv[0], "fg") == 0) return builtin_fg(argc, argv);
    if (strcmp(argv[0], "bg") == 0) return builtin_bg(argc, argv);
    if (strcmp(argv[0], "return") == 0) return handle_return(argc, argv);
    
    if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            printf("%s%s", argv[i], (i == argc - 1) ? "" : " ");
        }
        printf("\n");
        fflush(stdout);
        return 0;
    }
    
    if (strcmp(argv[0], "[") == 0 || strcmp(argv[0], "test") == 0) {
        int is_bracket = (argv[0][0] == '[');
        int real_argc = argc;
        if (is_bracket) {
            if (strcmp(argv[argc-1], "]") != 0) {
                fprintf(stderr, "%s: [: missing `]'\n", shell_var_get_name());
                return 2;
            }
            real_argc--;
        }
        
        /* [ ] or test with no args is false */
        if (real_argc == 1) return 1;

        if (real_argc == 2) {
            /* [ string ] or test string -> true if string not empty */
            return (argv[1][0] == '\0');
        }

        if (real_argc == 3) {
            if (strcmp(argv[1], "-z") == 0) return (argv[2][0] == '\0') ? 0 : 1;
            if (strcmp(argv[1], "-n") == 0) return (argv[2][0] != '\0') ? 0 : 1;
        }
        
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
            /* POSIX: If the first operand is an unsigned decimal integer, 
               reset each specified signal to its default value. */
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
            } else if (action[0] == '\0') {
                /* trap "" sig: ignore the signal */
                if (sig > 0) {
                    signal(sig, SIG_IGN);
                }
            } else {
                trap_commands[sig] = strdup(action);
                if (sig > 0) {
                    signal(sig, trap_handler);
                }
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
>>>>>>> pr-231
