#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

volatile sig_atomic_t sig_received = 0;

void handler(int sig) {
    sig_received = sig;
}

int main(int argc, char **argv) {
    printf("--- Signal Management Test ---\n");

    // 1. Test basic handler
    signal(SIGUSR1, handler);
    printf("Sending SIGUSR1 to myself...\n");
    kill(getpid(), SIGUSR1);
    if (sig_received == SIGUSR1) {
        printf("[OK] Basic handler delivery\n");
    } else {
        printf("[FAIL] Basic handler delivery (received %d)\n", sig_received);
    }

    // 2. Test SA_RESTART
    sig_received = 0;
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR2, &sa, NULL);

    printf("Test SA_RESTART: Handled signal during sleep should restart sleep...\n");
    // (In Substrate, sleep might be a syscall that supports restart)
    // We'll use a pipe read to test interruption
    int fds[2];
    pipe(fds);
    
    pid_t pid = fork();
    if (pid == 0) {
        usleep(100000); // 100ms
        kill(getppid(), SIGUSR2);
        exit(0);
    }

    char c;
    // This read should be interrupted by SIGUSR2, then RESTARTED
    // If it's NOT restarted, it should return -1 with EINTR.
    // However, if we don't send data, it will block again.
    // So we'll have the child send data AFTER the signal.
    
    printf("Parent: reading from pipe (should block, get sig, then block again)...\n");
    
    // We'll manually check interruption by NOT using SA_RESTART first
    sa.sa_flags = 0; // No restart
    sigaction(SIGUSR1, &sa, NULL);
    
    pid_t pid2 = fork();
    if (pid2 == 0) {
        usleep(50000); // 50ms
        kill(getppid(), SIGUSR1);
        exit(0);
    }
    
    ssize_t n = read(fds[0], &c, 1);
    if (n == -1 && errno == EINTR) {
        printf("[OK] read interrupted by signal (no SA_RESTART)\n");
    } else {
        printf("[FAIL] read NOT interrupted by signal (n=%ld, errno=%d)\n", (long)n, errno);
    }
    
    waitpid(pid2, NULL, 0);

    // Now test with SA_RESTART
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR2, &sa, NULL);
    
    pid_t pid3 = fork();
    if (pid3 == 0) {
        usleep(50000); // 50ms
        kill(getppid(), SIGUSR2);
        usleep(50000); // Another 50ms
        write(fds[1], "X", 1);
        exit(0);
    }

    printf("Parent: reading from pipe with SA_RESTART...\n");
    n = read(fds[0], &c, 1);
    if (n == 1 && c == 'X') {
        printf("[OK] read RESTARTED by signal (SA_RESTART)\n");
    } else {
        printf("[FAIL] read NOT restarted by signal (n=%ld, errno=%d)\n", (long)n, errno);
    }
    
    waitpid(pid3, NULL, 0);
    waitpid(pid, NULL, 0);

    // 3. Test fork inheritance
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    
    pid = fork();
    if (pid == 0) {
        sigset_t childmask;
        sigprocmask(SIG_BLOCK, NULL, &childmask);
        if (sigismember(&childmask, SIGUSR1)) {
            printf("[OK] Signal mask inherited by fork\n");
        } else {
            printf("[FAIL] Signal mask NOT inherited by fork\n");
        }
        exit(0);
    }
    waitpid(pid, NULL, 0);
    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    // 4. Test exec preservation of pending signals
    // To do this, we block a signal, send it, then exec a helper.
    if (argc == 1) {
        sigemptyset(&mask);
        sigaddset(&mask, SIGUSR1);
        sigprocmask(SIG_BLOCK, &mask, NULL);
        kill(getpid(), SIGUSR1);
        
        printf("Execing myself to test pending signal preservation...\n");
        char *new_argv[] = {argv[0], "exec_test", NULL};
        execve(argv[0], new_argv, NULL);
        perror("execve");
        exit(1);
    } else if (strcmp(argv[1], "exec_test") == 0) {
        sigset_t pending;
        sigpending(&pending);
        if (sigismember(&pending, SIGUSR1)) {
            printf("[OK] Pending signal preserved across exec\n");
        } else {
            printf("[FAIL] Pending signal NOT preserved across exec\n");
        }
    }

    printf("--- Signal Management Test Complete ---\n");
    return 0;
}
