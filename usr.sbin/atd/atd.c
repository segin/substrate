#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

/* Defines default tunables for batch daemon */
#define DEFAULT_LOAD_LIMIT 1.5
#define DEFAULT_BATCH_INTERVAL 60

int main(int argc, char *argv[]) {
    double load_limit = DEFAULT_LOAD_LIMIT;
    int batch_interval = DEFAULT_BATCH_INTERVAL;
    int daemonize = 1;
    
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
                fprintf(stderr, "usage: atd [-l load_avg] [-b batch_interval] [-f|-d]\n");
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
    }

    /* 
     * Daemon main loop goes here. 
     * It scans /var/spool/at and executes routines via libat.
     */
     
    // Dummy loop to prove out the structure
    while (1) {
        // 1. check load average against load_limit
        // 2. check time since last batch job against batch_interval
        // 3. fetch eligible jobs
        // 4. fork/exec at_exec routines
        
        sleep(batch_interval > 0 ? batch_interval : 60);
    }

    return 0;
}
