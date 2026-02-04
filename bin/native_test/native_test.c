/*
 * Native Substrate personality test program (libc version)
 * Lists / and /bin directories (long format), shows utsname, exits
 * 
 * Uses Substrate libc for all calls.
 * Properly detects and displays symbolic links with their targets.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <errno.h>


/* Format mode bits as ls-style string */
static void format_mode(mode_t mode, char *buf) {
    /* Determine file type */
    switch (mode & S_IFMT) {
        case S_IFDIR:  buf[0] = 'd'; break;
        case S_IFLNK:  buf[0] = 'l'; break;
        case S_IFREG:  buf[0] = '-'; break;
        default:       buf[0] = '?'; break;
    }
    buf[1] = (mode & 0400) ? 'r' : '-';
    buf[2] = (mode & 0200) ? 'w' : '-';
    buf[3] = (mode & 0100) ? 'x' : '-';
    buf[4] = (mode & 0040) ? 'r' : '-';
    buf[5] = (mode & 0020) ? 'w' : '-';
    buf[6] = (mode & 0010) ? 'x' : '-';
    buf[7] = (mode & 0004) ? 'r' : '-';
    buf[8] = (mode & 0002) ? 'w' : '-';
    buf[9] = (mode & 0001) ? 'x' : '-';
    buf[10] = '\0';
}

/* List directory contents in long format with symlink targets */
static void list_dir(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        printf("  [Cannot open directory]\n");
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char fullpath[512];
        struct stat st;
        char mode_str[12];
        char link_target[256];
        
        /* Build full path */
        strcpy(fullpath, path);
        if (path[strlen(path) - 1] != '/') {
            strcat(fullpath, "/");
        }
        strcat(fullpath, entry->d_name);
        
        /* Use lstat to detect symlinks (doesn't follow them) */
        if (lstat(fullpath, &st) == 0) {
            format_mode(st.st_mode, mode_str);
            
            /* Check if it's a symlink and read target */
            if ((st.st_mode & S_IFMT) == S_IFLNK) {
                ssize_t len = readlink(fullpath, link_target, sizeof(link_target) - 1);
                if (len > 0) {
                    link_target[len] = '\0';
                    printf("  %s %8lu  %s -> %s\n", mode_str, 
                           (unsigned long)st.st_size, entry->d_name, link_target);
                } else {
                    printf("  %s %8lu  %s -> [error]\n", mode_str,
                           (unsigned long)st.st_size, entry->d_name);
                }
            } else {
                printf("  %s %8lu  %s\n", mode_str, 
                       (unsigned long)st.st_size, entry->d_name);
            }
        } else {
            printf("  ?????????? ????????  %s\n", entry->d_name);
        }
    }
    
    closedir(dir);
}

int main(int argc, char **argv) {
    struct utsname uts;
    
    (void)argc;
    (void)argv;
    
    printf("=== Native Substrate Personality Test (libc) ===\n\n");
    
    /* Show utsname */
    printf("System Information:\n");
    if (uname(&uts) == 0) {
        printf("  sysname:  %s\n", uts.sysname);
        printf("  nodename: %s\n", uts.nodename);
        printf("  release:  %s\n", uts.release);
        printf("  version:  %s\n", uts.version);
        printf("  machine:  %s\n", uts.machine);
    } else {
        printf("  [uname failed]\n");
    }
    printf("\n");
    
    /* List / */
    printf("Contents of /:\n");
    list_dir("/");
    printf("\n");
    
    /* List /bin */
    printf("Contents of /bin:\n");
    list_dir("/bin");
    printf("\n");
    
    /* List /dev */
    printf("Contents of /dev:\n");
    list_dir("/dev");
    printf("\n");
    
    /* List /proc */
    printf("Contents of /proc:\n");
    list_dir("/proc");
    printf("\n");
    
    /* Fork Test */
    printf("Testing fork() and getpid():\n");
    printf("  Parent: PID=%d (Before Fork)\n", getpid());
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("  fork failed");
    } else if (pid == 0) {
        /* Child */
        printf("  Child:  PID=%d (Returned from getpid())\n", getpid());
        exit(0);
    } else {
        /* Parent */
        printf("  Parent: PID=%d (Returned from getpid())\n", getpid());
        printf("  Parent: Child PID=%d created\n", pid);
        int status;
        waitpid(pid, &status, 0);
        printf("  Parent: Child exited\n");
    }

    printf("\n");
    printf("=== Test Complete ===\n");
    return 0;
}
