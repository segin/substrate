#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../../sys/include/sys/sysinfo.h"

int sys_proc_list(pid_t *pids, size_t count);
int sys_proc_info(pid_t pid, sys_procinfo_t *info);

int ps_main(int argc, char *argv[]);

#define main ps_main
#include "../../../bin/ps/ps.c"
#undef main

int sys_proc_count(void) {
    return 2;
}

int sys_proc_list(pid_t *pids, size_t count) {
    if (!pids || count == 0) {
        return 2;
    }
    if (count >= 2) {
        pids[0] = 1;
        pids[1] = 2;
        return 2;
    }
    return 0;
}

int sys_proc_info(pid_t pid, sys_procinfo_t *info) {
    memset(info, 0, sizeof(*info));
    if (pid == 0 || pid == 1) {
        info->pid = 1;
        info->ppid = 0;
        info->uid = 0;
        info->euid = 0;
        info->gid = 0;
        info->egid = 0;
        info->state = SYS_PROC_STATE_RUN;
        info->user_time = 100;
        info->sys_time = 50;
        info->bitness = BITNESS_32;
        info->tty = 0;
        strcpy(info->name, "init");
        return 0;
    } else if (pid == 2) {
        info->pid = 2;
        info->ppid = 1;
        info->uid = 0;
        info->euid = 0;
        info->gid = 0;
        info->egid = 0;
        info->state = SYS_PROC_STATE_SLEEP;
        info->user_time = 720000;
        info->sys_time = 360000;
        info->bitness = BITNESS_64;
        info->tty = 0;
        strcpy(info->name, "bash");
        return 0;
    }
    return -1;
}

static void test_state_to_stat_mapping(void) {
    assert(strcmp(state_to_stat(SYS_PROC_STATE_IDLE), "I") == 0);
    assert(strcmp(state_to_stat(SYS_PROC_STATE_RUN), "R") == 0);
    assert(strcmp(state_to_stat(SYS_PROC_STATE_SLEEP), "S") == 0);
    assert(strcmp(state_to_stat(SYS_PROC_STATE_STOP), "T") == 0);
    assert(strcmp(state_to_stat(SYS_PROC_STATE_ZOMBIE), "Z") == 0);
    assert(strcmp(state_to_stat(SYS_PROC_STATE_DYING), "X") == 0);
    assert(strcmp(state_to_stat(0), "?") == 0);
    printf("ps state mapping passed.\n");
}

void test_output(int bitness) {
    char temp_file[] = "/tmp/ps_test_XXXXXX";
    int fd = mkstemp(temp_file);
    assert(fd != -1);
    
    int old_stdout = dup(1);
    dup2(fd, 1);
    
    char *argv_normal[] = {"ps", NULL};
    char *argv_bitness[] = {"ps", "-b", NULL};
    
    if (bitness) {
        ps_main(2, argv_bitness);
    } else {
        ps_main(1, argv_normal);
    }
    
    fflush(stdout);
    dup2(old_stdout, 1);
    close(old_stdout);
    
    lseek(fd, 0, SEEK_SET);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    read(fd, buf, sizeof(buf)-1);
    close(fd);
    unlink(temp_file);
    
    if (bitness) {
        assert(strstr(buf, "BITS") != NULL);
        assert(strstr(buf, "  32") != NULL);
        assert(strstr(buf, " init") != NULL);
        assert(strstr(buf, " bash") != NULL);
    } else {
        assert(strstr(buf, "BITS") == NULL);
        assert(strstr(buf, " init") != NULL);
        assert(strstr(buf, " bash") != NULL);
    }
    printf("Output test with bitness=%d passed.\n", bitness);
}

int main(void) {
    test_state_to_stat_mapping();
    test_output(0);
    test_output(1);
    return 0;
}
