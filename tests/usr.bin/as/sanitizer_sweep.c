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
 * Each job is assembled in a child made by fork, which calls the
 * assembler's main(): the child begins from this process as it was
 * before any source was read, so no job sees what another left -- and
 * the assembler, which is a program and keeps what it likes between
 * its start and its end, is not asked to assemble twice.
 *
 * It works in the current directory, in files named t.s, t.o, p.o, out
 * and pout.
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

/* The assembler linked here, on t.s, in a child; the wait status. */
static int assemble_here(const char *mode) {
    char *argv[6];
    pid_t pid;
    int status = -1;

    argv[0] = (char *)"as";
    argv[1] = (char *)mode;
    argv[2] = (char *)"-o";
    argv[3] = (char *)"t.o";
    argv[4] = (char *)"t.s";
    argv[5] = NULL;

    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        int fd = open("out", O_WRONLY | O_CREAT | O_TRUNC, 0600);

        if (fd < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0) {
            _exit(125);
        }
        close(fd);
        alarm(JOB_SECONDS);
        exit(as_main(5, argv));
    }
    if (waitpid(pid, &status, 0) != pid) {
        return -1;
    }
    return status;
}

/* The plain assembler, a program, on t.s; its exit status, or -1. */
static int assemble_plain(const char *plain, const char *mode) {
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

/* What became of the job whose source is t.s, in RESULT. */
static void judge(const char *plain, const char *mode, char *result, size_t result_sz) {
    int status, rc, plain_rc;
    char *said;
    const char *report;

    unlink("t.o");
    unlink("p.o");
    status = assemble_here(mode);
    said = slurp("out", NULL);
    report = said == NULL ? NULL : strstr(said, "runtime error");
    if (report == NULL && said != NULL) {
        report = strstr(said, "ERROR: AddressSanitizer");
    }

    if (report != NULL) {
        snprintf(result, result_sz, "FAIL %.100s", report);
        result[strcspn(result, "\n")] = '\0';
    } else if (status < 0 || !WIFEXITED(status)) {
        snprintf(result, result_sz, "FAIL killed by signal %d", status < 0 ? 0 : WTERMSIG(status));
    } else if (WEXITSTATUS(status) == 0) {
        snprintf(result, result_sz, "%s", is_elf("t.o") ? "ok" : "FAIL exit 0 and no object");
    } else if (WEXITSTATUS(status) == 1) {
        if (access("t.o", F_OK) == 0) {
            snprintf(result, result_sz, "FAIL refused, and an object written all the same");
        } else if (said == NULL || strstr(said, "error") == NULL) {
            snprintf(result, result_sz, "FAIL exit 1 with no message");
        } else {
            snprintf(result, result_sz, "refused");
        }
    } else {
        snprintf(result, result_sz, "FAIL exit %d", WEXITSTATUS(status));
    }
    free(said);

    if (strncmp(result, "FAIL", 4) == 0) {
        return;
    }
    rc = WEXITSTATUS(status);
    plain_rc = assemble_plain(plain, mode);
    if (plain_rc != rc) {
        snprintf(result, result_sz, "FAIL the plain build exits %d and the sanitizer build %d", plain_rc, rc);
    } else if (rc == 0 && !same_file("t.o", "p.o")) {
        snprintf(result, result_sz, "FAIL the plain build and the sanitizer build write different objects");
    }
}

int main(int argc, char **argv) {
    char line[LINE_MAX_BYTES];
    char result[256];

    if (argc != 2) {
        fprintf(stderr, "usage: %s plain-assembler < jobs\n", argv[0]);
        return 2;
    }
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *mode = line;
        char *ending, *text;
        FILE *src;

        line[strcspn(line, "\n")] = '\0';
        ending = strchr(mode, '\t');
        text = ending == NULL ? NULL : strchr(ending + 1, '\t');
        if (text == NULL) {
            fprintf(stderr, "%s: a job that is not mode, ending and text: %s\n", argv[0], line);
            return 2;
        }
        *ending++ = '\0';
        *text++ = '\0';

        src = fopen("t.s", "wb");
        if (src == NULL || fputs(text, src) == EOF || (ending[0] == 'n' && fputc('\n', src) == EOF) ||
            fclose(src) != 0) {
            fprintf(stderr, "%s: t.s cannot be written\n", argv[0]);
            return 2;
        }
        judge(argv[1], mode, result, sizeof(result));
        printf("%s\t%s\t%s\t%s\n", result, mode, ending, text);
    }
    return 0;
}
