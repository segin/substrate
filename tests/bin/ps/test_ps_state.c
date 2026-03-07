#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#define _SYS_SYSCALL_H

#define SYS_PROC_INFO 1
#define SYS_PROC_LIST 2

// Include the real definition since ps.c includes it anyway, but we need it here
#include "../../../sys/include/sys/sysinfo.h"

long syscall_mock(long number, ...);
#define syscall syscall_mock

int ps_main(int argc, char *argv[]);

#define main ps_main
#include "../../../bin/ps/ps.c"
#undef main

long syscall_mock(long number, ...) {
    if (number == SYS_PROC_LIST) {
        va_list ap;
        va_start(ap, number);
        uintptr_t pids_ptr = va_arg(ap, uintptr_t);
        size_t count = va_arg(ap, size_t);
        va_end(ap);
        
        if (count >= 2) {
            pid_t *pids = (pid_t*)(uintptr_t)pids_ptr;
            pids[0] = 1;
            pids[1] = 2;
            return 2;
        }
        return 0;
    } else if (number == SYS_PROC_INFO) {
        va_list ap;
        va_start(ap, number);
        pid_t pid = va_arg(ap, pid_t);
        uintptr_t info_ptr = va_arg(ap, uintptr_t);
        va_end(ap);
        
        sys_procinfo_t *info = (sys_procinfo_t*)(uintptr_t)info_ptr;
        memset(info, 0, sizeof(*info));
        if (pid == 1) {
            info->pid = 1;
            info->ppid = 0;
            info->uid = 0;
            info->gid = 0;
            info->state = 2; // RUN
            info->user_time = 100;
            info->sys_time = 50;
            info->bitness = 32;
            strcpy(info->name, "init");
            return 0;
        } else if (pid == 2) {
            info->pid = 2;
            info->ppid = 1;
            info->uid = 1000;
            info->gid = 1000;
            info->state = 3; // SLP
            info->user_time = 720000;
            info->sys_time = 360000;
            info->bitness = 64;
            strcpy(info->name, "bash");
            return 0;
        }
        return -1;
    }
    return -1;
}

void test_state_to_char(void) {
    assert(strcmp(state_to_char(1), "IDL") == 0);
    assert(strcmp(state_to_char(2), "RUN") == 0);
    assert(strcmp(state_to_char(3), "SLP") == 0);
    assert(strcmp(state_to_char(4), "STP") == 0);
    assert(strcmp(state_to_char(5), "ZOM") == 0);
    assert(strcmp(state_to_char(6), "DIE") == 0);
    assert(strcmp(state_to_char(0), "???") == 0);
    assert(strcmp(state_to_char(7), "???") == 0);
    assert(strcmp(state_to_char(255), "???") == 0);
    printf("All ps state_to_char unit tests passed.\n");
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
        assert(strstr(buf, "  32 RUN      ") != NULL);
        assert(strstr(buf, "  64 SLP      ") != NULL);
    } else {
        assert(strstr(buf, "BITS") == NULL);
        assert(strstr(buf, "RUN      ") != NULL);
        assert(strstr(buf, "SLP      ") != NULL);
    }
    printf("Output test with bitness=%d passed.\n", bitness);
}

int main(void) {
    test_state_to_char();
    test_output(0);
    test_output(1);
    return 0;
}
