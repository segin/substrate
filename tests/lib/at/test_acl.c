#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <assert.h>

#include <at.h>

void create_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

void remove_file(const char *path) {
    unlink(path);
}

int main() {
    printf("Starting ACL tests...\n");

    const char *allow_path = "/tmp/test_at.allow";
    const char *deny_path = "/tmp/test_at.deny";

    uid_t test_uid = getuid();
    struct passwd *pw = getpwuid(test_uid);
    assert(pw != NULL && "Test needs a valid user");
    const char *user = pw->pw_name;

    char content_user[128];
    snprintf(content_user, sizeof(content_user), "%s\n", user);

    // Test 1: allow exists, user listed -> success
    create_file(allow_path, content_user);
    remove_file(deny_path);
    assert(at_acl_check_access(test_uid) == 0);

    // Test 2: allow exists, user NOT listed -> fail
    create_file(allow_path, "nobody\n");
    assert(at_acl_check_access(test_uid) == -1);

    // Test 3: allow does not exist, deny exists and user listed -> fail
    remove_file(allow_path);
    create_file(deny_path, content_user);
    assert(at_acl_check_access(test_uid) == -1);

    // Test 4: allow does not exist, deny exists and user NOT listed -> success
    create_file(deny_path, "nobody\n");
    assert(at_acl_check_access(test_uid) == 0);

    // Test 5: neither exists -> fail (unless root)
    remove_file(allow_path);
    remove_file(deny_path);
    if (test_uid != 0) {
        assert(at_acl_check_access(test_uid) == -1);
    } else {
        assert(at_acl_check_access(test_uid) == 0);
    }

    // Test 6: root always succeeds
    create_file(deny_path, "root\n"); // Even if root is in deny
    assert(at_acl_check_access(0) == 0);

    // Cleanup
    remove_file(allow_path);
    remove_file(deny_path);

    printf("ACL tests completed successfully.\n");
    return 0;
}
