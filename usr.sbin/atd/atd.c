#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>

#include <at.h>

#define AT_SPOOL_ROOT "/var/spool/at"
#define AT_RUNNING_QUEUE '='
#define MAX_ATD_PATH 512

/* Defines default tunables for batch daemon */
#define DEFAULT_LOAD_LIMIT 1.5
#define DEFAULT_BATCH_INTERVAL 60

static void
usage(void) {
    fprintf(stderr, "usage: atd [-l load_avg] [-b batch_interval] [-f|-d]\n");
}

static int
is_valid_job_id(const char *name) {
    size_t i;

    if (!name || !name[0]) return 0;
    for (i = 0; name[i] != '\0'; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '.') return 0;
    }
    return 1;
}

static int
queue_is_batch(int queue_name) {
    return queue_name == 'b' || (queue_name >= 'A' && queue_name <= 'Z');
}

static int
ensure_dir(const char *path, mode_t mode) {
    if (mkdir(path, mode) == 0 || errno == EEXIST) return 0;
    return -1;
}

static pid_t
atd_setsid(void) {
    return (pid_t)syscall(SYS_SETSID);
}

static int
read_load_average(double *out_load) {
    FILE *fp;
    double load;

    if (!out_load) return -1;

    fp = fopen("/proc/loadavg", "r");
    if (!fp) return -1;

    if (fscanf(fp, "%lf", &load) != 1) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out_load = load;
    return 0;
}

static int
build_job_request(char queue_name, const struct stat *st, struct batch_submit_request *req) {
    if (!st || !req) return -1;

    memset(req, 0, sizeof(*req));
    req->queue = (char)queue_name;
    req->deferred_time = st->st_mtime;
    req->mail_mode = AT_MAIL_ON_OUTPUT;
    req->shell_policy = AT_SHELL_BOURNE;
    req->profile = queue_is_batch(queue_name) ? AT_PROFILE_GNU_EXTENDED
                                              : AT_PROFILE_POSIX_STRICT;
    req->submitter_uid = st->st_uid;
    req->submitter_gid = st->st_gid;

    return 0;
}

static int
claim_job_for_running(int queue_name, const char *job_name, char *claimed_path, size_t claimed_path_sz) {
    char src_path[MAX_ATD_PATH];
    size_t root_len;
    size_t name_len;

    if (!job_name || !claimed_path || claimed_path_sz == 0) return -1;
    if (ensure_dir(AT_SPOOL_ROOT, 0700) != 0) return -1;

    root_len = strlen(AT_SPOOL_ROOT);
    name_len = strlen(job_name);
    if (root_len + name_len + 4 >= sizeof(src_path) ||
        root_len + name_len + 4 >= claimed_path_sz) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(src_path, AT_SPOOL_ROOT, root_len);
    src_path[root_len] = '/';
    src_path[root_len + 1] = (char)queue_name;
    src_path[root_len + 2] = '/';
    memcpy(src_path + root_len + 3, job_name, name_len + 1);

    memcpy(claimed_path, AT_SPOOL_ROOT, root_len);
    claimed_path[root_len] = '/';
    claimed_path[root_len + 1] = (char)AT_RUNNING_QUEUE;
    claimed_path[root_len + 2] = '/';
    memcpy(claimed_path + root_len + 3, job_name, name_len + 1);

    if (ensure_dir("/var/spool/at/=", 0700) != 0) return -1;
    if (rename(src_path, claimed_path) != 0) return -1;
    return 0;
}

static int
run_spooled_job(int queue_name, const char *job_name, time_t *last_batch_run) {
    char claimed_path[MAX_ATD_PATH];
    struct stat st;
    struct batch_submit_request req;

    if (claim_job_for_running(queue_name, job_name, claimed_path, sizeof(claimed_path)) != 0) {
        return -1;
    }

    if (stat(claimed_path, &st) != 0) {
        unlink(claimed_path);
        return -1;
    }

    if (build_job_request(queue_name, &st, &req) != 0) {
        unlink(claimed_path);
        return -1;
    }

    (void)at_exec_run_job(&req, claimed_path);
    unlink(claimed_path);

    if (queue_is_batch(queue_name) && last_batch_run) *last_batch_run = time(NULL);
    return 0;
}

static int
process_queue_jobs(int queue_name, time_t now, int batch_allowed, time_t *last_batch_run) {
    DIR *dir;
    struct dirent *ent;
    char queue_path[MAX_ATD_PATH];
    int ran_jobs = 0;

    if (queue_name == AT_RUNNING_QUEUE) return 0;
    if (queue_is_batch(queue_name) && !batch_allowed) return 0;

    snprintf(queue_path, sizeof(queue_path), "%s/%c", AT_SPOOL_ROOT, queue_name);
    dir = opendir(queue_path);
    if (!dir) return 0;

    while ((ent = readdir(dir)) != NULL) {
        char job_path[MAX_ATD_PATH];
        struct stat st;
        size_t queue_path_len;
        size_t name_len;

        if (ent->d_name[0] == '.') continue;
        if (!is_valid_job_id(ent->d_name)) continue;

        queue_path_len = strlen(queue_path);
        name_len = strlen(ent->d_name);
        if (queue_path_len + name_len + 2 >= sizeof(job_path)) continue;

        memcpy(job_path, queue_path, queue_path_len);
        job_path[queue_path_len] = '/';
        memcpy(job_path + queue_path_len + 1, ent->d_name, name_len + 1);
        if (stat(job_path, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        if (st.st_mtime > now) continue;

        if (run_spooled_job(queue_name, ent->d_name, last_batch_run) == 0) {
            ran_jobs = 1;
            if (queue_is_batch(queue_name)) break;
        }
    }

    closedir(dir);
    return ran_jobs;
}

static int
dispatch_ready_jobs(double load_limit, int batch_interval, time_t *last_batch_run) {
    time_t now;
    double load_avg;
    int batch_allowed;
    int ran_any = 0;
    int queue_name;

    now = time(NULL);
    batch_allowed = 0;
    if (read_load_average(&load_avg) == 0) {
        if (load_avg <= load_limit &&
            (batch_interval <= 0 || !last_batch_run || *last_batch_run == 0 ||
             now - *last_batch_run >= batch_interval)) {
            batch_allowed = 1;
        }
    }

    for (queue_name = 'a'; queue_name <= 'z'; queue_name++) {
        ran_any |= process_queue_jobs(queue_name, now, batch_allowed, last_batch_run);
    }
    for (queue_name = 'A'; queue_name <= 'Z'; queue_name++) {
        ran_any |= process_queue_jobs(queue_name, now, batch_allowed, last_batch_run);
    }

    return ran_any;
}

int main(int argc, char *argv[]) {
    double load_limit = DEFAULT_LOAD_LIMIT;
    int batch_interval = DEFAULT_BATCH_INTERVAL;
    int daemonize = 1;
    time_t last_batch_run = 0;
    
    int opt;
    /* Phase 7.3: GNU daemon extensions (-l load, -b interval) */
    while ((opt = getopt(argc, argv, "l:b:fd")) != -1) {
        switch (opt) {
            case 'l':
                load_limit = strtod(optarg, NULL);
                if (load_limit <= 0.0) {
                    fprintf(stderr, "atd: load limit must be positive\n");
                    return 1;
                }
                break;
            case 'b':
                batch_interval = atoi(optarg);
                if (batch_interval < 0) {
                    fprintf(stderr, "atd: batch interval must be positive\n");
                    return 1;
                }
                break;
            case 'f':
            case 'd':
                daemonize = 0; // Run in foreground / debug
                break;
            default:
                usage();
                return 1;
        }
    }

    if (daemonize) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("atd: failed to fork");
            return 1;
        }
        if (pid > 0) exit(0);
        if (atd_setsid() < 0) {
            perror("atd: failed to detach session");
            return 1;
        }
    }

    while (1) {
        int ran_jobs;
        unsigned int sleep_for;

        ran_jobs = dispatch_ready_jobs(load_limit, batch_interval, &last_batch_run);
        if (ran_jobs) continue;

        sleep_for = batch_interval > 0 ? (unsigned int)batch_interval : 1U;
        sleep(sleep_for);
    }

    return 0;
}
