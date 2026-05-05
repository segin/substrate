#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#include <at.h>

#define AT_SPOOL_ROOT "/var/spool/at"
#define AT_SPOOL_TMP  "/var/spool/at/tmp"
#define AT_SPOOL_OUT  "/var/spool/at/spool"

static int ensure_dir(const char *path, mode_t mode) {
    if (mkdir(path, mode) == 0 || errno == EEXIST) return 0;
    return -1;
}

static int copy_fd_contents(int src_fd, int dst_fd) {
    char buf[4096];

    for (;;) {
        ssize_t nread = read(src_fd, buf, sizeof(buf));
        if (nread == 0) return 0;
        if (nread < 0) return -1;

        for (ssize_t off = 0; off < nread; ) {
            ssize_t nwritten = write(dst_fd, buf + off, (size_t)(nread - off));
            if (nwritten < 0) return -1;
            off += nwritten;
        }
    }
}

static int _generate_job_id(char *buf, size_t sz, uid_t uid) {
    time_t now = time(NULL);
    // REQ-BATCH-006: Alphanumeric and periods only.
    int n = snprintf(buf, sz, "%c%08lx.%04x", 
        'a', // arbitrary prefix
        (unsigned long)now, 
        uid & 0xFFFF);
    return (n > 0 && (size_t)n < sz) ? 0 : -1;
}

int at_spool_create_job(const struct batch_submit_request *req, struct batch_submit_result *out_res) {
    time_t sched_time;
    struct tm *info;
    char queue_dir[128];
    char tmp_template[160];
    char fin_path[128];
    int fd = -1;

    if (!req || !out_res) return -1;
    if (req->input_fd < 0) {
        snprintf(out_res->diagnostics, sizeof(out_res->diagnostics), "Invalid input file descriptor");
        out_res->status_code = 1;
        return -1;
    }

    // Phase 3.2: Generate Job ID
    if (_generate_job_id(out_res->job_id, sizeof(out_res->job_id), req->submitter_uid) != 0) {
        snprintf(out_res->diagnostics, sizeof(out_res->diagnostics), "Failed to generate job ID");
        out_res->status_code = 1;
        return -1;
    }

    out_res->effective_queue = req->queue;
    sched_time = req->deferred_time == 0 ? time(NULL) : req->deferred_time;

    // Phase 3.3: Format display string (user-timezone adjusted)
    info = localtime(&sched_time);
    strftime(out_res->scheduled_display_time, sizeof(out_res->scheduled_display_time),
             "%Y-%m-%d %H:%M:%S", info); // Arbitrary typical format

    if (ensure_dir(AT_SPOOL_ROOT, 0700) != 0 ||
        ensure_dir(AT_SPOOL_TMP, 0700) != 0 ||
        ensure_dir(AT_SPOOL_OUT, 0700) != 0) {
        snprintf(out_res->diagnostics, sizeof(out_res->diagnostics),
                 "Failed to prepare spool directories: %s", strerror(errno));
        out_res->status_code = 1;
        return -1;
    }

    snprintf(queue_dir, sizeof(queue_dir), "%s/%c", AT_SPOOL_ROOT, req->queue);
    if (ensure_dir(queue_dir, 0700) != 0) {
        snprintf(out_res->diagnostics, sizeof(out_res->diagnostics),
                 "Failed to prepare queue directory: %s", strerror(errno));
        out_res->status_code = 1;
        return -1;
    }

    snprintf(tmp_template, sizeof(tmp_template), "%s/%s.XXXXXX", AT_SPOOL_TMP, out_res->job_id);
    fd = mkstemp(tmp_template);
    if (fd < 0) {
        snprintf(out_res->diagnostics, sizeof(out_res->diagnostics),
                 "Failed to create temporary spool file: %s", strerror(errno));
        out_res->status_code = 1;
        return -1;
    }

    if (fchmod(fd, 0600) != 0 || fchown(fd, req->submitter_uid, req->submitter_gid) != 0) {
        snprintf(out_res->diagnostics, sizeof(out_res->diagnostics),
                 "Failed to set temporary spool file ownership: %s", strerror(errno));
        out_res->status_code = 1;
        unlink(tmp_template);
        close(fd);
        return -1;
    }

    if (copy_fd_contents(req->input_fd, fd) != 0) {
        snprintf(out_res->diagnostics, sizeof(out_res->diagnostics),
                 "Failed to write spool file: %s", strerror(errno));
        out_res->status_code = 1;
        unlink(tmp_template);
        close(fd);
        return -1;
    }

    close(fd);

    snprintf(fin_path, sizeof(fin_path), "%s/%c/%s", AT_SPOOL_ROOT, req->queue, out_res->job_id);
    if (rename(tmp_template, fin_path) != 0) {
        snprintf(out_res->diagnostics, sizeof(out_res->diagnostics),
                 "Failed to commit spool file: %s", strerror(errno));
        out_res->status_code = 1;
        unlink(tmp_template);
        return -1;
    }

    {
        struct timeval times[2];
        times[0].tv_sec = sched_time;
        times[0].tv_usec = 0;
        times[1].tv_sec = sched_time;
        times[1].tv_usec = 0;
        if (utimes(fin_path, times) != 0) {
            snprintf(out_res->diagnostics, sizeof(out_res->diagnostics),
                     "Failed to stamp spool file schedule: %s", strerror(errno));
            out_res->status_code = 1;
            unlink(fin_path);
            return -1;
        }
    }

    out_res->status_code = 0;
    out_res->profile = req->profile;
    out_res->diagnostics[0] = '\0';

    return 0;
}
