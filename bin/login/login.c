#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>

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

    struct termios oldt, newt;
    tcgetattr(0, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(0, TCSANOW, &newt);

    n = read(0, pass, 63);

    tcsetattr(0, TCSANOW, &oldt);
    printf("\n");

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

