#include <stdbool.h>
#include <string.h>
#include "../../../sys/exec/perso/freebsd/freebsd_user.h"
#include "../../../sys/sys/proc.h"

/*
 * Fuzz Test: Random kinfo_proc translations
 */

extern void mock_map_proc_to_kinfo(process_t *p, struct kinfo_proc *ki);

void fuzz_kinfo_translation(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (state / 65536);
    };

    process_t p;
    struct kinfo_proc ki;

    for (int i = 0; i < 1000; i++) {
        p.pid = (int)next_rand();
        
        // Randomize command name
        for (int j = 0; j < AC_COMM_LEN - 1; j++) {
            p.comm[j] = (char)(next_rand() % 255);
        }
        p.comm[AC_COMM_LEN - 1] = 0;

        mock_map_proc_to_kinfo(&p, &ki);
    }
}
