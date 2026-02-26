#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <time.h>

// Declarations
int sys_proc_info(pid_t pid, sys_procinfo_t *info) {
    return syscall(SYS_PROC_INFO, pid, (uint32_t)info, 0, 0, 0, 0);
}

int sys_proc_list(pid_t *pids, size_t count) {
    return syscall(SYS_PROC_LIST, (uint32_t)pids, count, 0, 0, 0, 0);
}

void print_time(uint32_t ticks) {
    // Assuming 100 ticks per second (HZ=100)
    uint32_t seconds = ticks / 100;
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds % 3600) / 60;
    uint32_t s = seconds % 60;
    printf("%02d:%02d:%02d", h, m, s);
}

const char *state_to_char(uint8_t state) {
    switch(state) {
        case 1: return "IDL"; // SIDL
        case 2: return "RUN"; // SRUN
        case 3: return "SLP"; // SSLEEP
        case 4: return "STP"; // SSTOP
        case 5: return "ZOM"; // SZOMB
        case 6: return "DIE"; // SDYING
        default: return "???";
    }
}

int main(int argc, char *argv[]) {
    int show_bitness = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bitness") == 0) {
            show_bitness = 1;
        }
    }
    
    // Get list of PIDs
    pid_t pids[128];
    int count = sys_proc_list(pids, 128);
    
    if (count < 0) {
        perror("sys_proc_list");
        return 1;
    }
    
    // Header
    if (show_bitness) {
        printf("%5s %5s %5s %5s %4s %-4s %9s %s\n", 
           "PID", "PPID", "UID", "GID", "BITS", "STAT", "TIME", "CMD");
    } else {
        printf("%5s %5s %5s %5s %-4s %9s %s\n", 
           "PID", "PPID", "UID", "GID", "STAT", "TIME", "CMD");
    }

    for (int i = 0; i < count; i++) {
        sys_procinfo_t info;
        if (sys_proc_info(pids[i], &info) == 0) {
            
            // Calc time (user + sys)
            uint32_t total_time = info.user_time + info.sys_time;
            
            if (show_bitness) {
                printf("%5d %5d %5d %5d %4d %s      ", 
                    info.pid, info.ppid, info.uid, info.gid, info.bitness, state_to_char(info.state));
                print_time(total_time);
                printf(" %s\n", info.name);
            } else {
                printf("%5d %5d %5d %5d %s      ", 
                    info.pid, info.ppid, info.uid, info.gid, state_to_char(info.state));
                print_time(total_time);
                printf(" %s\n", info.name);
            }
        }
    }
    
    return 0;
}