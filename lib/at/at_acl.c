#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

#include <at.h>

// Helper to check if username is in the given file
static int _is_user_in_file(const char *filename, const char *username) {
    FILE *f = fopen(filename, "r");
    if (!f) return 0;
    
    char line[128];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0'; // trim newline
        if (strcmp(line, username) == 0) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

#ifndef AT_ALLOW_FILE
#define AT_ALLOW_FILE "/etc/at.allow"
#endif

#ifndef AT_DENY_FILE
#define AT_DENY_FILE "/etc/at.deny"
#endif

// Returns 0 on success, < 0 on permission error
int at_acl_check_access(uid_t submitter) {
    if (submitter == 0) return 0; // Root always allowed

    struct passwd *pw = getpwuid(submitter);
    if (!pw || !pw->pw_name) {
        return -1; // Unknown user cannot submit
    }

    struct stat st;
    int allow_exists = (stat(AT_ALLOW_FILE, &st) == 0);
    int deny_exists = (stat(AT_DENY_FILE, &st) == 0);

    /* Phase 5 Access Control Logic: */
    /* If at.allow exists, only listed users can submit jobs. */
    if (allow_exists) {
        return _is_user_in_file(AT_ALLOW_FILE, pw->pw_name) ? 0 : -1;
    }

    /* If at.allow does not exist, but at.deny exists, users in at.deny are blocked.
       All others are permitted. */
    if (deny_exists) {
        return _is_user_in_file(AT_DENY_FILE, pw->pw_name) ? -1 : 0;
    }

    /* If neither at.allow nor at.deny exists, only privileged users (root) can submit.
       Since we already allowed root at the top, we just return -1 here. */
    return -1;
}
