/*
 * sanitizer_sweep AS < jobs > results
 *
 * The assembler, built with the sanitizers and linked into this program,
 * given each job as a source of its own, and the plain assembler AS
 * given the same.  A job is a line: the mode (-32 or -64), `n` if the
 * source ends in a newline and `-` if it does not, and the source's
 * text, with tabs between.  For each there is a line of results: what
 * became of it, then the job.
 *
 *   ok        exit 0 and an object, the same object AS writes
 *   refused   exit 1, a message and no object, and AS refuses it too
 *   FAIL ...  anything else, with what
 *
 * The assembler's main() is called, not run: starting a program built
 * with the sanitizers costs several times what assembling a line does,
 * and there are thousands of lines.  The calls are made one after
 * another in a child process.  The assembler is a program, though, and
 * may keep what it likes from one call to the next; so a call's result
 * stands only where AS, started afresh, ends the same way with the same
 * object.  Where it does not -- and where the child dies, as it does at
 * a sanitizer's report -- the job is done again in a child that has
 * assembled nothing before it, and that is the result given.  No
 * failure is reported but from a fresh start.
 *
 * It works in the current directory, in files named t.s, t.o, p.o, out,
 * pout and progress.
 */
#include "sanitizer_sweep.h"

#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define JOB_SECONDS 20
#define LINE_MAX_BYTES 4096
#define RESULT_MAX 256
/* The batch child's exit status when a job is to be done afresh. */
#define EXIT_AFRESH 77

struct job {
    char *mode;
    char *ending;
    char *text;
};

static struct job *jobs;
static size_t job_count;
static const char *plain;

/* The whole of a small file, with a NUL after it; NULL if there is none. */
static char *slurp(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    char *buf;
    size_t cap = 1 << 16;
    size_t len;

    if (f == NULL) {
        return NULL;
    }
    buf = malloc(cap + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    len = fread(buf, 1, cap, f);
    fclose(f);
    buf[len] = '\0';
    if (len_out != NULL) {
        *len_out = len;
    }
    return buf;
}

static int is_elf(const char *path) {
    size_t len = 0;
    char *buf = slurp(path, &len);
    int yes = buf != NULL && len >= 4 && memcmp(buf, "\177ELF", 4) == 0;

    free(buf);
    return yes;
}

static int same_file(const char *a, const char *b) {
    size_t alen = 0, blen = 0;
    char *abuf = slurp(a, &alen);
    char *bbuf = slurp(b, &blen);
    int same = abuf != NULL && bbuf != NULL && alen == blen && memcmp(abuf, bbuf, alen) == 0;

    free(abuf);
    free(bbuf);
    return same;
}

static void say(size_t index, const char *result) {
    printf("%s\t%s\t%s\t%s\n", result, jobs[index].mode, jobs[index].ending, jobs[index].text);
    fflush(stdout);
}

static int write_source(size_t index) {
    FILE *src = fopen("t.s", "wb");

    if (src == NULL) {
        return -1;
    }
    if (fputs(jobs[index].text, src) == EOF || (jobs[index].ending[0] == 'n' && fputc('\n', src) == EOF)) {
        fclose(src);
        return -1;
    }
    return fclose(src);
}

/* Which job the batch child is in, for when the child is gone. */
static void note_progress(size_t index) {
    FILE *f = fopen("progress", "wb");

    if (f != NULL) {
        fprintf(f, "%lu\n", (unsigned long)index);
        fclose(f);
    }
}

static size_t read_progress(size_t fallback) {
    char *text = slurp("progress", NULL);
    size_t index = fallback;

    if (text != NULL) {
        index = (size_t)strtoul(text, NULL, 10);
    }
    free(text);
    return index;
}

/* The assembler linked here, on t.s, what it says going to `out`. */
static int call_assembler(const char *mode) {
    char *argv[6];
    int out, saved_out, saved_err, rc;

    argv[0] = (char *)"as";
    argv[1] = (char *)mode;
    argv[2] = (char *)"-o";
    argv[3] = (char *)"t.o";
    argv[4] = (char *)"t.s";
    argv[5] = NULL;

    fflush(NULL);
    out = open("out", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    saved_out = dup(1);
    saved_err = dup(2);
    if (out < 0 || saved_out < 0 || saved_err < 0 || dup2(out, 1) < 0 || dup2(out, 2) < 0) {
        _exit(125);
    }
    close(out);
    alarm(JOB_SECONDS);
    rc = as_main(5, argv);
    alarm(0);
    fflush(NULL);
    dup2(saved_out, 1);
    dup2(saved_err, 2);
    close(saved_out);
    close(saved_err);
    return rc;
}

/* The plain assembler, a program, on t.s; its exit status, or -1. */
static int run_plain(const char *mode) {
    posix_spawn_file_actions_t actions;
    char *argv[6];
    char *envp[1] = { NULL };
    pid_t pid;
    int status;

    argv[0] = (char *)plain;
    argv[1] = (char *)mode;
    argv[2] = (char *)"-o";
    argv[3] = (char *)"p.o";
    argv[4] = (char *)"t.s";
    argv[5] = NULL;

    if (posix_spawn_file_actions_init(&actions) != 0) {
        return -1;
    }
    posix_spawn_file_actions_addopen(&actions, 1, "pout", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    if (posix_spawn(&pid, plain, &actions, NULL, argv, envp) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }
    posix_spawn_file_actions_destroy(&actions);
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

/*
 * What an end of the assembler amounts to: STATUS is its wait status, or
 * its return value made into one, and `out` and t.o are what it left.
 * Then the plain assembler, which must end the same way.
 */
static void judge(const char *mode, int status, char *result) {
    char *said = slurp("out", NULL);
    const char *report = said == NULL ? NULL : strstr(said, "runtime error");
    int rc, plain_rc;

    if (report == NULL && said != NULL) {
        report = strstr(said, "ERROR: AddressSanitizer");
    }
    if (report != NULL) {
        snprintf(result, RESULT_MAX, "FAIL %.100s", report);
        result[strcspn(result, "\n")] = '\0';
    } else if (!WIFEXITED(status)) {
        snprintf(result, RESULT_MAX, "FAIL killed by signal %d", WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    } else if (WEXITSTATUS(status) == 0) {
        snprintf(result, RESULT_MAX, "%s", is_elf("t.o") ? "ok" : "FAIL exit 0 and no object");
    } else if (WEXITSTATUS(status) == 1) {
        if (access("t.o", F_OK) == 0) {
            snprintf(result, RESULT_MAX, "FAIL refused, and an object written all the same");
        } else if (said == NULL || strstr(said, "error") == NULL) {
            snprintf(result, RESULT_MAX, "FAIL exit 1 with no message");
        } else {
            snprintf(result, RESULT_MAX, "refused");
        }
    } else {
        snprintf(result, RESULT_MAX, "FAIL exit %d", WEXITSTATUS(status));
    }
    free(said);
    if (strncmp(result, "FAIL", 4) == 0) {
        return;
    }

    rc = WEXITSTATUS(status);
    unlink("p.o");
    plain_rc = run_plain(mode);
    if (plain_rc != rc) {
        snprintf(result, RESULT_MAX, "FAIL the plain build exits %d and the sanitizer build %d", plain_rc, rc);
    } else if (rc == 0 && !same_file("t.o", "p.o")) {
        snprintf(result, RESULT_MAX, "FAIL the plain build and the sanitizer build write different objects");
    }
}

/* One job in a child that has assembled nothing: the result that stands. */
static void judge_afresh(size_t index, char *result) {
    pid_t pid;
    int status;

    unlink("t.o");
    if (write_source(index) != 0) {
        snprintf(result, RESULT_MAX, "FAIL the source cannot be written");
        return;
    }
    fflush(NULL);
    pid = fork();
    if (pid == 0) {
        int rc = call_assembler(jobs[index].mode);

        fflush(NULL);
        _exit(rc);
    }
    if (pid < 0 || waitpid(pid, &status, 0) != pid) {
        snprintf(result, RESULT_MAX, "FAIL no process to assemble in");
        return;
    }
    judge(jobs[index].mode, status, result);
}

/*
 * The jobs from FIRST on, one after another in this process, each said
 * as it is found good.  Ends, with EXIT_AFRESH, at the first that is
 * not: the parent does that one again.
 */
static void batch(size_t first) {
    char result[RESULT_MAX];
    size_t i;

    for (i = first; i < job_count; i++) {
        int rc;

        note_progress(i);
        unlink("t.o");
        if (write_source(i) != 0) {
            _exit(EXIT_AFRESH);
        }
        rc = call_assembler(jobs[i].mode);
        /* A return value as the wait status of a process that exited with it. */
        judge(jobs[i].mode, (rc & 0xff) << 8, result);
        if (strncmp(result, "FAIL", 4) == 0) {
            fflush(NULL);
            _exit(EXIT_AFRESH);
        }
        say(i, result);
    }
    fflush(NULL);
    _exit(0);
}

static int read_jobs(void) {
    char line[LINE_MAX_BYTES];
    size_t cap = 0;

    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *ending, *text;

        line[strcspn(line, "\n")] = '\0';
        ending = strchr(line, '\t');
        text = ending == NULL ? NULL : strchr(ending + 1, '\t');
        if (text == NULL) {
            fprintf(stderr, "a job that is not mode, ending and text: %s\n", line);
            return -1;
        }
        *ending++ = '\0';
        *text++ = '\0';
        if (job_count == cap) {
            struct job *more;

            cap = cap == 0 ? 1024 : cap * 2;
            more = realloc(jobs, cap * sizeof(*jobs));
            if (more == NULL) {
                return -1;
            }
            jobs = more;
        }
        jobs[job_count].mode = strdup(line);
        jobs[job_count].ending = strdup(ending);
        jobs[job_count].text = strdup(text);
        if (jobs[job_count].mode == NULL || jobs[job_count].ending == NULL || jobs[job_count].text == NULL) {
            return -1;
        }
        job_count++;
    }
    return 0;
}

int main(int argc, char **argv) {
    size_t next = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s plain-assembler < jobs\n", argv[0]);
        return 2;
    }
    plain = argv[1];
    if (read_jobs() != 0) {
        return 2;
    }

    while (next < job_count) {
        char result[RESULT_MAX];
        size_t stopped_at;
        pid_t pid;
        int status;

        note_progress(next);
        fflush(NULL);
        pid = fork();
        if (pid < 0) {
            perror("fork");
            return 2;
        }
        if (pid == 0) {
            batch(next);
        }
        if (waitpid(pid, &status, 0) != pid) {
            perror("waitpid");
            return 2;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            break;
        }

        /* The batch stopped, or died, in the job it last noted. */
        stopped_at = read_progress(next);
        if (stopped_at < next || stopped_at >= job_count) {
            stopped_at = next;
        }
        judge_afresh(stopped_at, result);
        say(stopped_at, result);
        next = stopped_at + 1;
    }
    return 0;
}
