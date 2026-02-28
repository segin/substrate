#include <stdio.h>
#include <sys/utsname.h>

int main() {
    struct utsname u;
    if (uname(&u) == 0) {
        printf("%s %s %s %s %s\n", u.sysname, u.nodename, u.release, u.version, u.machine);
    } else {
        printf("uname: error\n");
    }
    return 0;
}

