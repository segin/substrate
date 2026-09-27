/*
 * torture_at_output.c -- a non-root at job's output is captured.
 *
 * The job child opened /var/spool/at/spool/<job>.out only after taking the
 * owner's identity; the spool is root's and mode 0700, so for any owner but
 * root the open failed and the output went wherever atd's did.  This test
 * runs at_exec_run_job() for uid 1000 on a job that prints a line and
 * checks the .out file holds it and belongs to the job's owner.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <at.h>

#define JOB     "/tmp/atout-job"
#define OUTFILE "/var/spool/at/spool/atout-job.out"

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

int main(void) {
    printf("torture_at_output\n");
    mkdir("/tmp", 01777);
    unlink(OUTFILE);

    int fd = open(JOB, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const char *script = "echo at-output-marker\n";
    write(fd, script, strlen(script));
    close(fd);

    struct batch_submit_request req;
    memset(&req, 0, sizeof(req));
    req.submitter_uid = 1000;
    req.submitter_gid = 1000;
    req.umask_snapshot = 022;
    int rc = at_exec_run_job(&req, JOB);
    check("job ran", rc == 0, "job did not exit 0");

    char buf[128] = "";
    fd = open(OUTFILE, O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        buf[n > 0 ? n : 0] = '\0';
        close(fd);
    }
    check("output captured", strstr(buf, "at-output-marker") != NULL,
          fd < 0 ? "no .out file" : "marker not in .out file");

    struct stat st;
    check("output owned by the job's owner",
          stat(OUTFILE, &st) == 0 && st.st_uid == 1000 && st.st_gid == 1000,
          "wrong owner");

    unlink(OUTFILE);
    unlink(JOB);
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
