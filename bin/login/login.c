#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <stdlib.h>

static int verify_password(const char *user, const char *pass) {
    FILE *f = fopen("/etc/shadow", "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;

        char *colon1 = strchr(line, ':');
        if (!colon1) continue;
        *colon1 = '\0';

        char *uname = line;

        char *colon2 = strchr(colon1 + 1, ':');
        if (!colon2) continue;
        *colon2 = '\0';

        char *upass = colon1 + 1;

        if (strcmp(uname, user) == 0) {
            fclose(f);
            return strcmp(upass, pass) == 0;
        }
    }

    fclose(f);
    return 0;
}

static int read_password_no_echo(int fd, char *buffer, size_t size) {
    struct termios oldt, newt;
    int n;
    int term_ok = tcgetattr(fd, &oldt) == 0;

    if (term_ok) {
        newt = oldt;
        newt.c_lflag &= ~ECHO;
        tcsetattr(fd, TCSANOW, &newt);
    }

    n = read(fd, buffer, size);

    if (term_ok) {
        tcsetattr(fd, TCSANOW, &oldt);
    }
    printf("\n");

    return n;
}

int main() {
    char user[64];
    char pass[64];
    
    printf("TestUnix Login\n");
    printf("Username: ");
    fflush(stdout);
    
    // Simple read
    int n = read(0, user, 63);
    if(n>0) user[n-1] = 0; // strip \n
    
    printf("Password: ");
    fflush(stdout);

    n = read_password_no_echo(0, pass, 63);
    if(n>0) pass[n-1] = 0;

    if (verify_password(user, pass)) {
        printf("Login successful.\n");
        // exec shell
        char *args[] = {"/bin/sh", NULL};
        execve("/bin/sh", args, NULL);
    } else {
        printf("Login failed.\n");
    }
    return 0;
}
