#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "exec.h"
#include "job.h"
#include "util.h"
#include <sys/wait.h>

job_t *first_job = NULL;

void job_init(void) {
    first_job = NULL;
}

job_t *job_new(void) {
    job_t *j = calloc(1, sizeof(job_t));
    if (!j) return NULL;
    j->stdin_fd = -1;
    j->stdout_fd = -1;
    j->stderr_fd = -1;
    
    // Assign job ID
    int max_id = 0;
    job_t *it = first_job;
    while (it) {
        if (it->id > max_id) max_id = it->id;
        it = it->next;
    }
    j->id = max_id + 1;

    // Add to tail
    if (!first_job) {
        first_job = j;
    } else {
        job_t *last = first_job;
        while (last->next) last = last->next;
        last->next = j;
    }
    return j;
}

void job_free(job_t *j) {
    if (!j) return;
    // Remove from list
    if (first_job == j) {
        first_job = j->next;
    } else {
        job_t *prev = first_job;
        while (prev && prev->next != j) prev = prev->next;
        if (prev) prev->next = j->next;
    }

    if (j->command) free(j->command);
    process_t *p = j->first_process;
    while (p) {
        process_t *next = p->next;
        if (p->argv) {
            for (int i = 0; p->argv[i]; i++) free(p->argv[i]);
            free(p->argv);
        }
        free(p);
        p = next;
    }
    free(j);
}

void job_add_process(job_t *j, pid_t pid, char **argv) {
    if (!j) return;
    process_t *p = calloc(1, sizeof(process_t));
    if (!p) return;
    p->pid = pid;

    // Deep copy argv
    if (argv) {
        int argc = 0;
        while (argv[argc]) argc++;
        p->argv = malloc((argc + 1) * sizeof(char *));
        if (!p->argv) { free(p); return; }
        for (int i = 0; i < argc; i++) p->argv[i] = strdup(argv[i]);
        p->argv[argc] = NULL;
    }

    // Add to tail
    if (!j->first_process) {
        j->first_process = p;
    } else {
        process_t *last = j->first_process;
        while (last->next) last = last->next;
        last->next = p;
    }
}

job_t *find_job(pid_t pgid) {
    job_t *j = first_job;
    while (j) {
        if (j->pgid == pgid) return j;
        j = j->next;
    }
    return NULL;
}

job_t *job_find(const char *name) {
    if (!name) return NULL;
    if (name[0] == '%') {
        if (name[1] == '%' || name[1] == '+') {
            job_t *j = first_job;
            if (j) while (j->next) j = j->next;
            return j;
        } else if (name[1] == '-') {
            job_t *j = first_job;
            job_t *prev = NULL;
            if (j) while (j->next) { prev = j; j = j->next; }
            return prev ? prev : j;
        }
        int id = atoi(name + 1);
        job_t *j = first_job;
        while (j && j->id != id) j = j->next;
        return j;
    } else {
        pid_t pid = atoi(name);
        // Try PGID first
        job_t *j = find_job(pid);
        if (j) return j;
        // Try process PID
        for (j = first_job; j; j = j->next) {
            process_t *p;
            for (p = j->first_process; p; p = p->next) {
                if (p->pid == pid) return j;
            }
        }
    }
    return NULL;
}

int job_is_stopped(job_t *j) {
    if (!j) return 0;
    process_t *p;
    for (p = j->first_process; p; p = p->next) {
        if (!p->completed && !p->stopped)
            return 0;
    }
    return 1;
}

int job_is_completed(job_t *j) {
    if (!j) return 0;
    process_t *p;
    for (p = j->first_process; p; p = p->next) {
        if (!p->completed)
            return 0;
    }
    return 1;
}

void job_print_info(job_t *j, const char *status) {
    fprintf(stderr, "[%d] %d %s\t%s\n", j->id, (int)j->pgid, status, j->command ? j->command : "(unknown)");
}

int job_mark_process_status(pid_t pid, int status) {
    job_t *j;
    process_t *p;

    if (pid > 0) {
        for (j = first_job; j; j = j->next) {
            for (p = j->first_process; p; p = p->next) {
                if (p->pid == pid) {
                    p->status = status;
                    if (WIFSTOPPED(status)) {
                        p->stopped = 1;
                        j->notified = 0;
                    } else {
                        p->completed = 1;
                        j->notified = 0;
                    }
                    return 0;
                }
            }
        }
    }
    return -1;
}

void job_update_status(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        job_mark_process_status(pid, status);
    }
    
    // Notify user of finished jobs
    job_t *j = first_job;
    while (j) {
        job_t *next = j->next;
        if (job_is_completed(j)) {
            if (!j->notified) {
                job_print_info(j, "Done");
            }
            job_free(j);
        } else if (job_is_stopped(j) && !j->notified) {
            job_print_info(j, "Stopped");
            j->notified = 1;
        }
        j = next;
    }
}

int job_wait(job_t *j) {
    int status;
    pid_t pid;

    while (!job_is_stopped(j) && !job_is_completed(j)) {
        pid = waitpid(-j->pgid, &status, WUNTRACED);
        if (pid > 0) {
            job_mark_process_status(pid, status);
        } else {
            break;
        }
    }

    // Return the status of the last process in the pipeline
    process_t *p = j->first_process;
    while (p && p->next) p = p->next;
    return p ? p->status : 0;
}

int builtin_jobs(int argc, char **argv) {
    int show_pids = 0;
    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        show_pids = 1;
    }
    job_update_status(); // Sync before listing
    job_t *j = first_job;
    while (j) {
        const char *status = "Running";
        if (job_is_completed(j)) status = "Done";
        else if (job_is_stopped(j)) status = "Stopped";
        
        if (show_pids) {
            printf("[%d] %d %s\t%s\n", j->id, (int)j->pgid, status, j->command ? j->command : "(unknown)");
        } else {
            printf("[%d] %s\t%s\n", j->id, status, j->command ? j->command : "(unknown)");
        }
        j = j->next;
    }
    return 0;
}


int builtin_fg(int argc, char **argv) {
    job_t *j = NULL;

    if (argc > 1) {
        j = job_find(argv[1]);
    } else {
        j = first_job;
        if (j) while (j->next) j = j->next;
    }

    if (!j) {
        fprintf(stderr, "fg: %s: no such job\n", argc > 1 ? argv[1] : "current");
        return 1;
    }

    printf("%s\n", j->command ? j->command : "(unknown)");
    
    if (tcsetpgrp(STDIN_FILENO, j->pgid) == -1) {
        perror("tcsetpgrp");
    }
    
    process_t *p;
    for (p = j->first_process; p; p = p->next) {
        p->stopped = 0;
    }

    kill(-j->pgid, SIGCONT);
    int status = job_wait(j);
    
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    tcsetattr(STDIN_FILENO, TCSADRAIN, &shell_tmodes);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

int builtin_bg(int argc, char **argv) {
    job_t *j = NULL;

    if (argc > 1) {
        j = job_find(argv[1]);
    } else {
        j = first_job;
        if (j) while (j->next) j = j->next;
    }

    if (!j) {
        fprintf(stderr, "bg: %s: no such job\n", argc > 1 ? argv[1] : "current");
        return 1;
    }

    process_t *p;
    for (p = j->first_process; p; p = p->next) p->stopped = 0;

    j->notified = 0;
    kill(-j->pgid, SIGCONT);
    job_print_info(j, "Running");
    return 0;
}
