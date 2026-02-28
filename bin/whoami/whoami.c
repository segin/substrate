#include <stdio.h>
#include <unistd.h>
#include <pwd.h>

int main() {
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        printf("%s\n", pw->pw_name);
    } else {
        printf("%d\n", uid);
    }
    return 0;
}

