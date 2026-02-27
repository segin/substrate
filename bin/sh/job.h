#ifndef SH_JOB_H
#define SH_JOB_H

#include <sys/types.h>
#include <termios.h>

typedef struct process {
    struct process *next;
    char **argv;
    pid_t pid;
    int completed;
    int stopped;
    int status;
} process_t;

typedef struct job {
    struct job *next;
    int id;                // Job ID (1, 2, ...)
    char *command;
    process_t *first_process;
    pid_t pgid;
    int notified; // If 1, user has been notified of status change
    struct termios tmodes; // Saved terminal modes
    int stdin_fd, stdout_fd, stderr_fd; // Standard IO descriptors
} job_t;

extern job_t *first_job;

void job_init(void);
job_t *job_new(void);
void job_free(job_t *j);
void job_add_process(job_t *j, pid_t pid, char **argv);
void job_print_info(job_t *j, const char *status);
job_t *find_job(pid_t pgid);
job_t *job_find(const char *name);
int job_is_stopped(job_t *j);
int job_is_completed(job_t *j);
void job_update_status(void);
int job_wait(job_t *j);

int builtin_jobs(int argc, char **argv);
int builtin_fg(int argc, char **argv);
int builtin_bg(int argc, char **argv);

#endif
