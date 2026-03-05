#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>

static int read_password_no_echo(int fd, char *buffer, size_t size) {
    struct termios oldt, newt;
    int n;

    tcgetattr(fd, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(fd, TCSANOW, &newt);

    n = read(fd, buffer, size);

    tcsetattr(fd, TCSANOW, &oldt);
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
    
    if (strcmp(user, "root") == 0 && strcmp(pass, "root") == 0) {
        printf("Login successful.\n");
        // exec shell
        // execve("/bin/sh", ...);
    } else {
        printf("Login failed.\n");
    }
    return 0;
}

