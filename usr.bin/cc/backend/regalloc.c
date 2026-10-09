#include "cc_backend.h"

int cc_backend_pick_spill_victim(const int *reg_values, const int *next_use, const unsigned char *dirty, int reg_count,
                                 int avoid, int prefer) {
    int i;
    int victim = -1;
    int farthest = -1;

    if (reg_values == NULL || reg_count <= 0) {
        return(-1);
    }

    if (prefer >= 0 && prefer < reg_count && prefer != avoid && reg_values[prefer] < 0) {
        return(prefer);
    }

    for (i = 0; i < reg_count; ++i) {
        if (i == avoid) {
            continue;
        }
        if (reg_values[i] < 0) {
            return(i);
        }
    }

    if (prefer >= 0 && prefer < reg_count && prefer != avoid) {
        victim = prefer;
    }

    for (i = 0; i < reg_count; ++i) {
        int nu;
        if (i == avoid) {
            continue;
        }
        if (reg_values[i] < 0) {
            return(i);
        }

        nu = -1;
        if (next_use != NULL) {
            nu = next_use[i];
        }
        if (nu < 0) {
            if (dirty == NULL || dirty[i] == 0) {
                return(i);
            }
            if (victim < 0) {
                victim = i;
                farthest = nu;
            }
            continue;
        }
        if (victim < 0 || nu > farthest) {
            victim = i;
            farthest = nu;
        }
    }

    if (victim < 0) {
        victim = 0;
    }
    return(victim);
}
