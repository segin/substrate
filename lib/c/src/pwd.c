#include <pwd.h>
#include <stddef.h>
#include <string.h>

// Mock implementation of user database
static struct passwd mock_passwd = {
    .pw_name = "root",
    .pw_passwd = "x",
    .pw_uid = 0,
    .pw_gid = 0,
    .pw_gecos = "Super User",
    .pw_dir = "/root",
    .pw_shell = "/bin/sh"
};

struct passwd *getpwuid(uid_t uid) {
    if (uid == 0) return &mock_passwd;
    return NULL;
}

struct passwd *getpwnam(const char *name) {
    if (strcmp(name, "root") == 0) return &mock_passwd;
    return NULL;
}
