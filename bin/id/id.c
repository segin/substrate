#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

int main() {
    uid_t uid = getuid();
    gid_t gid = getgid();
    struct passwd *pw = getpwuid(uid);
    struct group *gr = getgrgid(gid);
    
    printf("uid=%d(%s) gid=%d(%s)\n", 
           uid, pw ? pw->pw_name : "", 
           gid, gr ? gr->gr_name : "");
    return 0;
}

