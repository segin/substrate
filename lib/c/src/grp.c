#include <grp.h>
#include <stddef.h>

static struct group mock_group = {
    .gr_name = "root",
    .gr_passwd = "x",
    .gr_gid = 0,
    .gr_mem = NULL
};

struct group *getgrgid(gid_t gid) {
    if (gid == 0) return &mock_group;
    return NULL;
}

struct group *getgrnam(const char *name) {
    (void)name;
    return &mock_group; // Always root
}
