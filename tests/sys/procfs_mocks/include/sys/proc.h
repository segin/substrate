#ifndef _SYS_PROC_H
#define _SYS_PROC_H
#include <stdint.h>
#define AC_COMM_LEN 16
#define MAX_FD 32
typedef struct process {
    int pid;
    char comm[AC_COMM_LEN];
    uint32_t uid;
    uint32_t gid;
    int perso_id;
} process_t;
extern process_t processes[];
extern process_t *current_process;
process_t *proc_find(int pid);
int proc_get_last_pid(void);
#endif
