#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include <at.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FILE *f = fopen("/tmp/fuzz_at.allow", "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
    
    FILE *d = fopen("/tmp/fuzz_at.deny", "wb");
    if (d) {
        fwrite(data, 1, size, d);
        fclose(d);
    }

    uid_t u = getuid();

    // The ACL function will parse the fuzzed files
    at_acl_check_access(u);

    unlink("/tmp/fuzz_at.allow");
    unlink("/tmp/fuzz_at.deny");

    return 0;
}
