/*
 * Auto-generated comprehensive fuzzing test for pmap
 * Tests: create, destroy, enter, remove, protect, extract
 */

#include <sys/types.h>
#include "../arch/i386/pmap.h"
#include "../kern/console.h"

void run_pmap_fuzz_test(void) {
    kprint("\n=== PMAP Fuzzing Test (Comprehensive) ===\n");
    kprint("Testing random pmap operations...\n");

    pmap_t pmaps[285];
    for (int i = 0; i < 285; i++) pmaps[i] = 0;
    int ops_count = 0;

    // Op 0: Create pmap 0
    pmaps[0] = pmap_create();
    if (!pmaps[0]) kprint("Warning: pmap_create failed for 0\n");
    ops_count++;
    // Op 1: Remove pmap 0 va=0xbdd65000
    if (pmaps[0]) {
        pmap_remove(pmaps[0], 3184939008);
    }
    ops_count++;
    // Op 2: Enter pmap 0 va=0x23b8d000 pa=0xbc996000 prot=0x1
    if (pmaps[0]) {
        pmap_enter(pmaps[0], 599314432, 3164168192, 1, 0);
    }
    ops_count++;
    // Op 3: Remove pmap 0 va=0x23b8d000
    if (pmaps[0]) {
        pmap_remove(pmaps[0], 599314432);
    }
    ops_count++;
    // Op 4: Create pmap 1
    pmaps[1] = pmap_create();
    if (!pmaps[1]) kprint("Warning: pmap_create failed for 1\n");
    ops_count++;
    // Op 5: Create pmap 2
    pmaps[2] = pmap_create();
    if (!pmaps[2]) kprint("Warning: pmap_create failed for 2\n");
    ops_count++;
    // Op 6: Destroy pmap 2
    if (pmaps[2]) {
        pmap_destroy(pmaps[2]);
        pmaps[2] = 0;
    }
    ops_count++;
    // Op 7: Create pmap 3
    pmaps[3] = pmap_create();
    if (!pmaps[3]) kprint("Warning: pmap_create failed for 3\n");
    ops_count++;
    // Op 8: Destroy pmap 3
    if (pmaps[3]) {
        pmap_destroy(pmaps[3]);
        pmaps[3] = 0;
    }
    ops_count++;
    // Op 9: Destroy pmap 1
    if (pmaps[1]) {
        pmap_destroy(pmaps[1]);
        pmaps[1] = 0;
    }
    ops_count++;
    // Op 10: Remove pmap 0 va=0x4722a000
    if (pmaps[0]) {
        pmap_remove(pmaps[0], 1193451520);
    }
    ops_count++;
    // Op 11: Destroy pmap 0
    if (pmaps[0]) {
        pmap_destroy(pmaps[0]);
        pmaps[0] = 0;
    }
    ops_count++;
    // Op 12: Create pmap 4
    pmaps[4] = pmap_create();
    if (!pmaps[4]) kprint("Warning: pmap_create failed for 4\n");
    ops_count++;
    // Op 13: Create pmap 5
    pmaps[5] = pmap_create();
    if (!pmaps[5]) kprint("Warning: pmap_create failed for 5\n");
    ops_count++;
    // Op 14: Enter pmap 5 va=0x580d8000 pa=0x9a9dc000 prot=0x5
    if (pmaps[5]) {
        pmap_enter(pmaps[5], 1477279744, 2594029568, 5, 0);
    }
    ops_count++;
    // Op 15: Protect pmap 5 va=0x580d8000
    if (pmaps[5]) {
        pmap_protect(pmaps[5], 1477279744, 1477283840, 15);
    }
    ops_count++;
    // Op 16: Create pmap 6
    pmaps[6] = pmap_create();
    if (!pmaps[6]) kprint("Warning: pmap_create failed for 6\n");
    ops_count++;
    // Op 17: Enter pmap 6 va=0x9e575000 pa=0xe2bcf000 prot=0x5
    if (pmaps[6]) {
        pmap_enter(pmaps[6], 2656522240, 3804033024, 5, 0);
    }
    ops_count++;
    // Op 18: Remove pmap 6 va=0x9e575000
    if (pmaps[6]) {
        pmap_remove(pmaps[6], 2656522240);
    }
    ops_count++;
    // Op 19: Create pmap 7
    pmaps[7] = pmap_create();
    if (!pmaps[7]) kprint("Warning: pmap_create failed for 7\n");
    ops_count++;
    // Op 20: Destroy pmap 6
    if (pmaps[6]) {
        pmap_destroy(pmaps[6]);
        pmaps[6] = 0;
    }
    ops_count++;
    // Op 21: Extract pmap 4 va=0x61500000
    if (pmaps[4]) {
        pmap_extract(pmaps[4], 1632632832);
    }
    ops_count++;
    // Op 22: Enter pmap 7 va=0x5d65b000 pa=0x29b3b000 prot=0x5
    if (pmaps[7]) {
        pmap_enter(pmaps[7], 1566945280, 699641856, 5, 0);
    }
    ops_count++;
    // Op 23: Enter pmap 7 va=0x4458b000 pa=0xb3ba7000 prot=0x1
    if (pmaps[7]) {
        pmap_enter(pmaps[7], 1146662912, 3015340032, 1, 0);
    }
    ops_count++;
    // Op 24: Remove pmap 4 va=0x88bd7000
    if (pmaps[4]) {
        pmap_remove(pmaps[4], 2294116352);
    }
    ops_count++;
    // Op 25: Enter pmap 5 va=0xa3d71000 pa=0xb03b6000 prot=0x3
    if (pmaps[5]) {
        pmap_enter(pmaps[5], 2748780544, 2956681216, 3, 0);
    }
    ops_count++;
    // Op 26: Remove pmap 4 va=0x3aa2f000
    if (pmaps[4]) {
        pmap_remove(pmaps[4], 983756800);
    }
    ops_count++;
    // Op 27: Protect pmap 5 va=0xa3d71000
    if (pmaps[5]) {
        pmap_protect(pmaps[5], 2748780544, 2748784640, 15);
    }
    ops_count++;
    // Op 28: Create pmap 8
    pmaps[8] = pmap_create();
    if (!pmaps[8]) kprint("Warning: pmap_create failed for 8\n");
    ops_count++;
    // Op 29: Extract pmap 7 va=0x4458b000
    if (pmaps[7]) {
        pmap_extract(pmaps[7], 1146662912);
    }
    ops_count++;
    // Op 30: Enter pmap 8 va=0x24934000 pa=0x43df2000 prot=0x3
    if (pmaps[8]) {
        pmap_enter(pmaps[8], 613629952, 1138696192, 3, 0);
    }
    ops_count++;
    // Op 31: Destroy pmap 7
    if (pmaps[7]) {
        pmap_destroy(pmaps[7]);
        pmaps[7] = 0;
    }
    ops_count++;
    // Op 32: Protect pmap 5 va=0xa3d71000
    if (pmaps[5]) {
        pmap_protect(pmaps[5], 2748780544, 2748784640, 15);
    }
    ops_count++;
    // Op 33: Destroy pmap 4
    if (pmaps[4]) {
        pmap_destroy(pmaps[4]);
        pmaps[4] = 0;
    }
    ops_count++;
    // Op 34: Enter pmap 5 va=0xc0fe000 pa=0xdc813000 prot=0x1
    if (pmaps[5]) {
        pmap_enter(pmaps[5], 202366976, 3699453952, 1, 0);
    }
    ops_count++;
    // Op 35: Destroy pmap 5
    if (pmaps[5]) {
        pmap_destroy(pmaps[5]);
        pmaps[5] = 0;
    }
    ops_count++;
    // Op 36: Protect pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_protect(pmaps[8], 613629952, 613634048, 15);
    }
    ops_count++;
    // Op 37: Enter pmap 8 va=0x87741000 pa=0x406ca000 prot=0x1
    if (pmaps[8]) {
        pmap_enter(pmaps[8], 2272530432, 1080860672, 1, 0);
    }
    ops_count++;
    // Op 38: Remove pmap 8 va=0x87741000
    if (pmaps[8]) {
        pmap_remove(pmaps[8], 2272530432);
    }
    ops_count++;
    // Op 39: Protect pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_protect(pmaps[8], 613629952, 613634048, 15);
    }
    ops_count++;
    // Op 40: Enter pmap 8 va=0xd4b000 pa=0xf43d4000 prot=0x5
    if (pmaps[8]) {
        pmap_enter(pmaps[8], 13938688, 4097654784, 5, 0);
    }
    ops_count++;
    // Op 41: Extract pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_extract(pmaps[8], 613629952);
    }
    ops_count++;
    // Op 42: Extract pmap 8 va=0x81f64000
    if (pmaps[8]) {
        pmap_extract(pmaps[8], 2180399104);
    }
    ops_count++;
    // Op 43: Remove pmap 8 va=0xd4b000
    if (pmaps[8]) {
        pmap_remove(pmaps[8], 13938688);
    }
    ops_count++;
    // Op 44: Protect pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_protect(pmaps[8], 613629952, 613634048, 15);
    }
    ops_count++;
    // Op 45: Create pmap 9
    pmaps[9] = pmap_create();
    if (!pmaps[9]) kprint("Warning: pmap_create failed for 9\n");
    ops_count++;
    // Op 46: Extract pmap 9 va=0x3da9d000
    if (pmaps[9]) {
        pmap_extract(pmaps[9], 1034539008);
    }
    ops_count++;
    // Op 47: Extract pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_extract(pmaps[8], 613629952);
    }
    ops_count++;
    // Op 48: Protect pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_protect(pmaps[8], 613629952, 613634048, 15);
    }
    ops_count++;
    // Op 49: Extract pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_extract(pmaps[8], 613629952);
    }
    ops_count++;
    // Op 50: Extract pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_extract(pmaps[8], 613629952);
    }
    ops_count++;
    // Op 51: Extract pmap 9 va=0x847fe000
    if (pmaps[9]) {
        pmap_extract(pmaps[9], 2222972928);
    }
    ops_count++;
    // Op 52: Enter pmap 8 va=0x3985d000 pa=0x10740000 prot=0x5
    if (pmaps[8]) {
        pmap_enter(pmaps[8], 965070848, 276037632, 5, 0);
    }
    ops_count++;
    // Op 53: Create pmap 10
    pmaps[10] = pmap_create();
    if (!pmaps[10]) kprint("Warning: pmap_create failed for 10\n");
    ops_count++;
    // Op 54: Remove pmap 10 va=0x38603000
    if (pmaps[10]) {
        pmap_remove(pmaps[10], 945827840);
    }
    ops_count++;
    // Op 55: Create pmap 11
    pmaps[11] = pmap_create();
    if (!pmaps[11]) kprint("Warning: pmap_create failed for 11\n");
    ops_count++;
    // Op 56: Protect pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_protect(pmaps[8], 613629952, 613634048, 1);
    }
    ops_count++;
    // Op 57: Extract pmap 10 va=0x3cede000
    if (pmaps[10]) {
        pmap_extract(pmaps[10], 1022222336);
    }
    ops_count++;
    // Op 58: Enter pmap 11 va=0x36d84000 pa=0x8a1b3000 prot=0x3
    if (pmaps[11]) {
        pmap_enter(pmaps[11], 920141824, 2317037568, 3, 0);
    }
    ops_count++;
    // Op 59: Protect pmap 11 va=0x36d84000
    if (pmaps[11]) {
        pmap_protect(pmaps[11], 920141824, 920145920, 15);
    }
    ops_count++;
    // Op 60: Create pmap 12
    pmaps[12] = pmap_create();
    if (!pmaps[12]) kprint("Warning: pmap_create failed for 12\n");
    ops_count++;
    // Op 61: Remove pmap 10 va=0x6c6fb000
    if (pmaps[10]) {
        pmap_remove(pmaps[10], 1819258880);
    }
    ops_count++;
    // Op 62: Enter pmap 8 va=0xac61a000 pa=0xa758d000 prot=0x1
    if (pmaps[8]) {
        pmap_enter(pmaps[8], 2892079104, 2807615488, 1, 0);
    }
    ops_count++;
    // Op 63: Create pmap 13
    pmaps[13] = pmap_create();
    if (!pmaps[13]) kprint("Warning: pmap_create failed for 13\n");
    ops_count++;
    // Op 64: Protect pmap 8 va=0x24934000
    if (pmaps[8]) {
        pmap_protect(pmaps[8], 613629952, 613634048, 1);
    }
    ops_count++;
    // Op 65: Destroy pmap 11
    if (pmaps[11]) {
        pmap_destroy(pmaps[11]);
        pmaps[11] = 0;
    }
    ops_count++;
    // Op 66: Create pmap 14
    pmaps[14] = pmap_create();
    if (!pmaps[14]) kprint("Warning: pmap_create failed for 14\n");
    ops_count++;
    // Op 67: Destroy pmap 12
    if (pmaps[12]) {
        pmap_destroy(pmaps[12]);
        pmaps[12] = 0;
    }
    ops_count++;
    // Op 68: Destroy pmap 8
    if (pmaps[8]) {
        pmap_destroy(pmaps[8]);
        pmaps[8] = 0;
    }
    ops_count++;
    // Op 69: Enter pmap 9 va=0xcf36000 pa=0xa702f000 prot=0x1
    if (pmaps[9]) {
        pmap_enter(pmaps[9], 217276416, 2801987584, 1, 0);
    }
    ops_count++;
    // Op 70: Extract pmap 10 va=0x7c530000
    if (pmaps[10]) {
        pmap_extract(pmaps[10], 2085814272);
    }
    ops_count++;
    // Op 71: Enter pmap 14 va=0xf02c000 pa=0x2a35a000 prot=0xf
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 251838464, 708157440, 15, 0);
    }
    ops_count++;
    // Op 72: Create pmap 15
    pmaps[15] = pmap_create();
    if (!pmaps[15]) kprint("Warning: pmap_create failed for 15\n");
    ops_count++;
    // Op 73: Enter pmap 14 va=0x49062000 pa=0x6c5a3000 prot=0xf
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 1225138176, 1817849856, 15, 0);
    }
    ops_count++;
    // Op 74: Destroy pmap 13
    if (pmaps[13]) {
        pmap_destroy(pmaps[13]);
        pmaps[13] = 0;
    }
    ops_count++;
    // Op 75: Destroy pmap 9
    if (pmaps[9]) {
        pmap_destroy(pmaps[9]);
        pmaps[9] = 0;
    }
    ops_count++;
    // Op 76: Remove pmap 15 va=0xf9af000
    if (pmaps[15]) {
        pmap_remove(pmaps[15], 261812224);
    }
    ops_count++;
    // Op 77: Create pmap 16
    pmaps[16] = pmap_create();
    if (!pmaps[16]) kprint("Warning: pmap_create failed for 16\n");
    ops_count++;
    // Op 78: Enter pmap 14 va=0xe8fb000 pa=0xf6059000 prot=0x1
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 244297728, 4127559680, 1, 0);
    }
    ops_count++;
    // Op 79: Extract pmap 10 va=0xacdac000
    if (pmaps[10]) {
        pmap_extract(pmaps[10], 2900017152);
    }
    ops_count++;
    // Op 80: Extract pmap 16 va=0x91d64000
    if (pmaps[16]) {
        pmap_extract(pmaps[16], 2446737408);
    }
    ops_count++;
    // Op 81: Destroy pmap 10
    if (pmaps[10]) {
        pmap_destroy(pmaps[10]);
        pmaps[10] = 0;
    }
    ops_count++;
    // Op 82: Remove pmap 15 va=0xa849a000
    if (pmaps[15]) {
        pmap_remove(pmaps[15], 2823397376);
    }
    ops_count++;
    // Op 83: Remove pmap 16 va=0x50fda000
    if (pmaps[16]) {
        pmap_remove(pmaps[16], 1358798848);
    }
    ops_count++;
    // Op 84: Extract pmap 14 va=0x49062000
    if (pmaps[14]) {
        pmap_extract(pmaps[14], 1225138176);
    }
    ops_count++;
    // Op 85: Destroy pmap 15
    if (pmaps[15]) {
        pmap_destroy(pmaps[15]);
        pmaps[15] = 0;
    }
    ops_count++;
    // Op 86: Create pmap 17
    pmaps[17] = pmap_create();
    if (!pmaps[17]) kprint("Warning: pmap_create failed for 17\n");
    ops_count++;
    // Op 87: Remove pmap 16 va=0x50f10000
    if (pmaps[16]) {
        pmap_remove(pmaps[16], 1357971456);
    }
    ops_count++;
    // Op 88: Extract pmap 14 va=0xe8fb000
    if (pmaps[14]) {
        pmap_extract(pmaps[14], 244297728);
    }
    ops_count++;
    // Op 89: Extract pmap 14 va=0xf02c000
    if (pmaps[14]) {
        pmap_extract(pmaps[14], 251838464);
    }
    ops_count++;
    // Op 90: Enter pmap 14 va=0x5958b000 pa=0xe1905000 prot=0x1
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 1498984448, 3784331264, 1, 0);
    }
    ops_count++;
    // Op 91: Extract pmap 16 va=0x702ce000
    if (pmaps[16]) {
        pmap_extract(pmaps[16], 1881989120);
    }
    ops_count++;
    // Op 92: Enter pmap 17 va=0x8768b000 pa=0x210b000 prot=0x5
    if (pmaps[17]) {
        pmap_enter(pmaps[17], 2271784960, 34648064, 5, 0);
    }
    ops_count++;
    // Op 93: Extract pmap 14 va=0x2260f000
    if (pmaps[14]) {
        pmap_extract(pmaps[14], 576778240);
    }
    ops_count++;
    // Op 94: Enter pmap 14 va=0xbe0f1000 pa=0x8db01000 prot=0x3
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 3188658176, 2377125888, 3, 0);
    }
    ops_count++;
    // Op 95: Enter pmap 17 va=0x35ebe000 pa=0xb7c56000 prot=0x5
    if (pmaps[17]) {
        pmap_enter(pmaps[17], 904650752, 3083165696, 5, 0);
    }
    ops_count++;
    // Op 96: Destroy pmap 17
    if (pmaps[17]) {
        pmap_destroy(pmaps[17]);
        pmaps[17] = 0;
    }
    ops_count++;
    // Op 97: Extract pmap 16 va=0xd013000
    if (pmaps[16]) {
        pmap_extract(pmaps[16], 218181632);
    }
    ops_count++;
    // Op 98: Create pmap 18
    pmaps[18] = pmap_create();
    if (!pmaps[18]) kprint("Warning: pmap_create failed for 18\n");
    ops_count++;
    // Op 99: Enter pmap 16 va=0xb495000 pa=0xf85000 prot=0x5
    if (pmaps[16]) {
        pmap_enter(pmaps[16], 189353984, 16273408, 5, 0);
    }
    ops_count++;
    kprint(".");
    // Op 100: Extract pmap 14 va=0xbe0f1000
    if (pmaps[14]) {
        pmap_extract(pmaps[14], 3188658176);
    }
    ops_count++;
    // Op 101: Create pmap 19
    pmaps[19] = pmap_create();
    if (!pmaps[19]) kprint("Warning: pmap_create failed for 19\n");
    ops_count++;
    // Op 102: Create pmap 20
    pmaps[20] = pmap_create();
    if (!pmaps[20]) kprint("Warning: pmap_create failed for 20\n");
    ops_count++;
    // Op 103: Extract pmap 16 va=0xb495000
    if (pmaps[16]) {
        pmap_extract(pmaps[16], 189353984);
    }
    ops_count++;
    // Op 104: Remove pmap 16 va=0xb495000
    if (pmaps[16]) {
        pmap_remove(pmaps[16], 189353984);
    }
    ops_count++;
    // Op 105: Create pmap 21
    pmaps[21] = pmap_create();
    if (!pmaps[21]) kprint("Warning: pmap_create failed for 21\n");
    ops_count++;
    // Op 106: Enter pmap 14 va=0x5b997000 pa=0x35d79000 prot=0x3
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 1536782336, 903319552, 3, 0);
    }
    ops_count++;
    // Op 107: Remove pmap 18 va=0x8f549000
    if (pmaps[18]) {
        pmap_remove(pmaps[18], 2404683776);
    }
    ops_count++;
    // Op 108: Extract pmap 19 va=0xbfddd000
    if (pmaps[19]) {
        pmap_extract(pmaps[19], 3218984960);
    }
    ops_count++;
    // Op 109: Destroy pmap 16
    if (pmaps[16]) {
        pmap_destroy(pmaps[16]);
        pmaps[16] = 0;
    }
    ops_count++;
    // Op 110: Extract pmap 18 va=0x6587000
    if (pmaps[18]) {
        pmap_extract(pmaps[18], 106459136);
    }
    ops_count++;
    // Op 111: Destroy pmap 19
    if (pmaps[19]) {
        pmap_destroy(pmaps[19]);
        pmaps[19] = 0;
    }
    ops_count++;
    // Op 112: Enter pmap 14 va=0x61ee5000 pa=0xdf565000 prot=0x1
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 1643008000, 3746975744, 1, 0);
    }
    ops_count++;
    // Op 113: Extract pmap 18 va=0x75d67000
    if (pmaps[18]) {
        pmap_extract(pmaps[18], 1976987648);
    }
    ops_count++;
    // Op 114: Enter pmap 18 va=0x39119000 pa=0x61ed000 prot=0x3
    if (pmaps[18]) {
        pmap_enter(pmaps[18], 957452288, 102682624, 3, 0);
    }
    ops_count++;
    // Op 115: Enter pmap 20 va=0x11c59000 pa=0xf7960000 prot=0x5
    if (pmaps[20]) {
        pmap_enter(pmaps[20], 298160128, 4153802752, 5, 0);
    }
    ops_count++;
    // Op 116: Enter pmap 21 va=0xadf4f000 pa=0xfb3ca000 prot=0x5
    if (pmaps[21]) {
        pmap_enter(pmaps[21], 2918510592, 4215054336, 5, 0);
    }
    ops_count++;
    // Op 117: Extract pmap 14 va=0x42df0000
    if (pmaps[14]) {
        pmap_extract(pmaps[14], 1121910784);
    }
    ops_count++;
    // Op 118: Destroy pmap 20
    if (pmaps[20]) {
        pmap_destroy(pmaps[20]);
        pmaps[20] = 0;
    }
    ops_count++;
    // Op 119: Create pmap 22
    pmaps[22] = pmap_create();
    if (!pmaps[22]) kprint("Warning: pmap_create failed for 22\n");
    ops_count++;
    // Op 120: Remove pmap 21 va=0xadf4f000
    if (pmaps[21]) {
        pmap_remove(pmaps[21], 2918510592);
    }
    ops_count++;
    // Op 121: Enter pmap 14 va=0x629c3000 pa=0xe655f000 prot=0x3
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 1654403072, 3864391680, 3, 0);
    }
    ops_count++;
    // Op 122: Enter pmap 22 va=0x6ee000 pa=0x85297000 prot=0x3
    if (pmaps[22]) {
        pmap_enter(pmaps[22], 7266304, 2234085376, 3, 0);
    }
    ops_count++;
    // Op 123: Enter pmap 14 va=0xaa0b8000 pa=0xebc7a000 prot=0x5
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 2852880384, 3955728384, 5, 0);
    }
    ops_count++;
    // Op 124: Remove pmap 14 va=0xbe0f1000
    if (pmaps[14]) {
        pmap_remove(pmaps[14], 3188658176);
    }
    ops_count++;
    // Op 125: Enter pmap 22 va=0x5380c000 pa=0x6713b000 prot=0x5
    if (pmaps[22]) {
        pmap_enter(pmaps[22], 1400946688, 1729343488, 5, 0);
    }
    ops_count++;
    // Op 126: Remove pmap 18 va=0x39119000
    if (pmaps[18]) {
        pmap_remove(pmaps[18], 957452288);
    }
    ops_count++;
    // Op 127: Remove pmap 22 va=0x6ee000
    if (pmaps[22]) {
        pmap_remove(pmaps[22], 7266304);
    }
    ops_count++;
    // Op 128: Remove pmap 21 va=0x67f49000
    if (pmaps[21]) {
        pmap_remove(pmaps[21], 1744080896);
    }
    ops_count++;
    // Op 129: Enter pmap 14 va=0x4dcac000 pa=0x49832000 prot=0x3
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 1305133056, 1233330176, 3, 0);
    }
    ops_count++;
    // Op 130: Enter pmap 21 va=0x77098000 pa=0x7128e000 prot=0xf
    if (pmaps[21]) {
        pmap_enter(pmaps[21], 1997111296, 1898504192, 15, 0);
    }
    ops_count++;
    // Op 131: Remove pmap 22 va=0x5380c000
    if (pmaps[22]) {
        pmap_remove(pmaps[22], 1400946688);
    }
    ops_count++;
    // Op 132: Remove pmap 21 va=0x77098000
    if (pmaps[21]) {
        pmap_remove(pmaps[21], 1997111296);
    }
    ops_count++;
    // Op 133: Create pmap 23
    pmaps[23] = pmap_create();
    if (!pmaps[23]) kprint("Warning: pmap_create failed for 23\n");
    ops_count++;
    // Op 134: Extract pmap 18 va=0x39821000
    if (pmaps[18]) {
        pmap_extract(pmaps[18], 964825088);
    }
    ops_count++;
    // Op 135: Create pmap 24
    pmaps[24] = pmap_create();
    if (!pmaps[24]) kprint("Warning: pmap_create failed for 24\n");
    ops_count++;
    // Op 136: Destroy pmap 22
    if (pmaps[22]) {
        pmap_destroy(pmaps[22]);
        pmaps[22] = 0;
    }
    ops_count++;
    // Op 137: Remove pmap 14 va=0x629c3000
    if (pmaps[14]) {
        pmap_remove(pmaps[14], 1654403072);
    }
    ops_count++;
    // Op 138: Enter pmap 24 va=0x31c69000 pa=0xb7f58000 prot=0xf
    if (pmaps[24]) {
        pmap_enter(pmaps[24], 835096576, 3086319616, 15, 0);
    }
    ops_count++;
    // Op 139: Enter pmap 18 va=0x25c74000 pa=0xa8036000 prot=0x1
    if (pmaps[18]) {
        pmap_enter(pmaps[18], 633815040, 2818793472, 1, 0);
    }
    ops_count++;
    // Op 140: Extract pmap 14 va=0x5958b000
    if (pmaps[14]) {
        pmap_extract(pmaps[14], 1498984448);
    }
    ops_count++;
    // Op 141: Destroy pmap 24
    if (pmaps[24]) {
        pmap_destroy(pmaps[24]);
        pmaps[24] = 0;
    }
    ops_count++;
    // Op 142: Enter pmap 18 va=0x1f116000 pa=0x74eaa000 prot=0x3
    if (pmaps[18]) {
        pmap_enter(pmaps[18], 521232384, 1961533440, 3, 0);
    }
    ops_count++;
    // Op 143: Extract pmap 23 va=0xb8226000
    if (pmaps[23]) {
        pmap_extract(pmaps[23], 3089260544);
    }
    ops_count++;
    // Op 144: Extract pmap 23 va=0x8c416000
    if (pmaps[23]) {
        pmap_extract(pmaps[23], 2353094656);
    }
    ops_count++;
    // Op 145: Enter pmap 18 va=0xbe604000 pa=0xdc9ae000 prot=0xf
    if (pmaps[18]) {
        pmap_enter(pmaps[18], 3193978880, 3701137408, 15, 0);
    }
    ops_count++;
    // Op 146: Enter pmap 18 va=0xa33dd000 pa=0x470de000 prot=0xf
    if (pmaps[18]) {
        pmap_enter(pmaps[18], 2738737152, 1192091648, 15, 0);
    }
    ops_count++;
    // Op 147: Remove pmap 21 va=0x709b8000
    if (pmaps[21]) {
        pmap_remove(pmaps[21], 1889239040);
    }
    ops_count++;
    // Op 148: Create pmap 25
    pmaps[25] = pmap_create();
    if (!pmaps[25]) kprint("Warning: pmap_create failed for 25\n");
    ops_count++;
    // Op 149: Enter pmap 21 va=0x55fa2000 pa=0x51e87000 prot=0x1
    if (pmaps[21]) {
        pmap_enter(pmaps[21], 1442455552, 1374187520, 1, 0);
    }
    ops_count++;
    // Op 150: Create pmap 26
    pmaps[26] = pmap_create();
    if (!pmaps[26]) kprint("Warning: pmap_create failed for 26\n");
    ops_count++;
    // Op 151: Destroy pmap 26
    if (pmaps[26]) {
        pmap_destroy(pmaps[26]);
        pmaps[26] = 0;
    }
    ops_count++;
    // Op 152: Destroy pmap 18
    if (pmaps[18]) {
        pmap_destroy(pmaps[18]);
        pmaps[18] = 0;
    }
    ops_count++;
    // Op 153: Create pmap 27
    pmaps[27] = pmap_create();
    if (!pmaps[27]) kprint("Warning: pmap_create failed for 27\n");
    ops_count++;
    // Op 154: Enter pmap 27 va=0x7746e000 pa=0x6a802000 prot=0x1
    if (pmaps[27]) {
        pmap_enter(pmaps[27], 2001133568, 1786781696, 1, 0);
    }
    ops_count++;
    // Op 155: Destroy pmap 25
    if (pmaps[25]) {
        pmap_destroy(pmaps[25]);
        pmaps[25] = 0;
    }
    ops_count++;
    // Op 156: Enter pmap 14 va=0x93608000 pa=0x6170a000 prot=0xf
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 2472574976, 1634770944, 15, 0);
    }
    ops_count++;
    // Op 157: Create pmap 28
    pmaps[28] = pmap_create();
    if (!pmaps[28]) kprint("Warning: pmap_create failed for 28\n");
    ops_count++;
    // Op 158: Enter pmap 27 va=0x6b450000 pa=0x89d8d000 prot=0x3
    if (pmaps[27]) {
        pmap_enter(pmaps[27], 1799684096, 2312687616, 3, 0);
    }
    ops_count++;
    // Op 159: Enter pmap 23 va=0x6f930000 pa=0x7c630000 prot=0x1
    if (pmaps[23]) {
        pmap_enter(pmaps[23], 1871904768, 2086862848, 1, 0);
    }
    ops_count++;
    // Op 160: Enter pmap 27 va=0xb9640000 pa=0x2a505000 prot=0xf
    if (pmaps[27]) {
        pmap_enter(pmaps[27], 3110338560, 709906432, 15, 0);
    }
    ops_count++;
    // Op 161: Extract pmap 28 va=0x64de9000
    if (pmaps[28]) {
        pmap_extract(pmaps[28], 1692307456);
    }
    ops_count++;
    // Op 162: Remove pmap 14 va=0x49062000
    if (pmaps[14]) {
        pmap_remove(pmaps[14], 1225138176);
    }
    ops_count++;
    // Op 163: Remove pmap 21 va=0x55fa2000
    if (pmaps[21]) {
        pmap_remove(pmaps[21], 1442455552);
    }
    ops_count++;
    // Op 164: Destroy pmap 23
    if (pmaps[23]) {
        pmap_destroy(pmaps[23]);
        pmaps[23] = 0;
    }
    ops_count++;
    // Op 165: Enter pmap 21 va=0x74673000 pa=0x53bc2000 prot=0x5
    if (pmaps[21]) {
        pmap_enter(pmaps[21], 1952919552, 1404837888, 5, 0);
    }
    ops_count++;
    // Op 166: Enter pmap 28 va=0x4094e000 pa=0xd5ccb000 prot=0x1
    if (pmaps[28]) {
        pmap_enter(pmaps[28], 1083498496, 3586961408, 1, 0);
    }
    ops_count++;
    // Op 167: Enter pmap 14 va=0x59971000 pa=0x39769000 prot=0x1
    if (pmaps[14]) {
        pmap_enter(pmaps[14], 1503072256, 964071424, 1, 0);
    }
    ops_count++;
    // Op 168: Protect pmap 14 va=0xf02c000
    if (pmaps[14]) {
        pmap_protect(pmaps[14], 251838464, 251842560, 1);
    }
    ops_count++;
    // Op 169: Destroy pmap 14
    if (pmaps[14]) {
        pmap_destroy(pmaps[14]);
        pmaps[14] = 0;
    }
    ops_count++;
    // Op 170: Remove pmap 21 va=0x74673000
    if (pmaps[21]) {
        pmap_remove(pmaps[21], 1952919552);
    }
    ops_count++;
    // Op 171: Enter pmap 21 va=0x90605000 pa=0xf2b03000 prot=0x3
    if (pmaps[21]) {
        pmap_enter(pmaps[21], 2422231040, 4071632896, 3, 0);
    }
    ops_count++;
    // Op 172: Enter pmap 27 va=0x5e6ff000 pa=0x2b043000 prot=0x1
    if (pmaps[27]) {
        pmap_enter(pmaps[27], 1584394240, 721694720, 1, 0);
    }
    ops_count++;
    // Op 173: Protect pmap 21 va=0x90605000
    if (pmaps[21]) {
        pmap_protect(pmaps[21], 2422231040, 2422235136, 1);
    }
    ops_count++;
    // Op 174: Remove pmap 27 va=0xb9640000
    if (pmaps[27]) {
        pmap_remove(pmaps[27], 3110338560);
    }
    ops_count++;
    // Op 175: Enter pmap 28 va=0x32c5c000 pa=0x13848000 prot=0x3
    if (pmaps[28]) {
        pmap_enter(pmaps[28], 851820544, 327450624, 3, 0);
    }
    ops_count++;
    // Op 176: Create pmap 29
    pmaps[29] = pmap_create();
    if (!pmaps[29]) kprint("Warning: pmap_create failed for 29\n");
    ops_count++;
    // Op 177: Protect pmap 21 va=0x90605000
    if (pmaps[21]) {
        pmap_protect(pmaps[21], 2422231040, 2422235136, 15);
    }
    ops_count++;
    // Op 178: Enter pmap 28 va=0x11a73000 pa=0x8196a000 prot=0x5
    if (pmaps[28]) {
        pmap_enter(pmaps[28], 296169472, 2174132224, 5, 0);
    }
    ops_count++;
    // Op 179: Create pmap 30
    pmaps[30] = pmap_create();
    if (!pmaps[30]) kprint("Warning: pmap_create failed for 30\n");
    ops_count++;
    // Op 180: Enter pmap 29 va=0x1b04a000 pa=0x6f0b6000 prot=0x5
    if (pmaps[29]) {
        pmap_enter(pmaps[29], 453287936, 1863016448, 5, 0);
    }
    ops_count++;
    // Op 181: Remove pmap 29 va=0x1b04a000
    if (pmaps[29]) {
        pmap_remove(pmaps[29], 453287936);
    }
    ops_count++;
    // Op 182: Enter pmap 30 va=0xa6847000 pa=0x45341000 prot=0xf
    if (pmaps[30]) {
        pmap_enter(pmaps[30], 2793697280, 1161039872, 15, 0);
    }
    ops_count++;
    // Op 183: Enter pmap 30 va=0x44b5a000 pa=0x52928000 prot=0x3
    if (pmaps[30]) {
        pmap_enter(pmaps[30], 1152753664, 1385332736, 3, 0);
    }
    ops_count++;
    // Op 184: Protect pmap 21 va=0x90605000
    if (pmaps[21]) {
        pmap_protect(pmaps[21], 2422231040, 2422235136, 15);
    }
    ops_count++;
    // Op 185: Destroy pmap 29
    if (pmaps[29]) {
        pmap_destroy(pmaps[29]);
        pmaps[29] = 0;
    }
    ops_count++;
    // Op 186: Remove pmap 30 va=0x44b5a000
    if (pmaps[30]) {
        pmap_remove(pmaps[30], 1152753664);
    }
    ops_count++;
    // Op 187: Create pmap 31
    pmaps[31] = pmap_create();
    if (!pmaps[31]) kprint("Warning: pmap_create failed for 31\n");
    ops_count++;
    // Op 188: Extract pmap 27 va=0x6b450000
    if (pmaps[27]) {
        pmap_extract(pmaps[27], 1799684096);
    }
    ops_count++;
    // Op 189: Protect pmap 28 va=0x32c5c000
    if (pmaps[28]) {
        pmap_protect(pmaps[28], 851820544, 851824640, 15);
    }
    ops_count++;
    // Op 190: Remove pmap 31 va=0x30e92000
    if (pmaps[31]) {
        pmap_remove(pmaps[31], 820584448);
    }
    ops_count++;
    // Op 191: Create pmap 32
    pmaps[32] = pmap_create();
    if (!pmaps[32]) kprint("Warning: pmap_create failed for 32\n");
    ops_count++;
    // Op 192: Protect pmap 30 va=0xa6847000
    if (pmaps[30]) {
        pmap_protect(pmaps[30], 2793697280, 2793701376, 15);
    }
    ops_count++;
    // Op 193: Remove pmap 30 va=0xa6847000
    if (pmaps[30]) {
        pmap_remove(pmaps[30], 2793697280);
    }
    ops_count++;
    // Op 194: Protect pmap 21 va=0x90605000
    if (pmaps[21]) {
        pmap_protect(pmaps[21], 2422231040, 2422235136, 1);
    }
    ops_count++;
    // Op 195: Enter pmap 27 va=0x4e639000 pa=0xaa094000 prot=0x5
    if (pmaps[27]) {
        pmap_enter(pmaps[27], 1315147776, 2852732928, 5, 0);
    }
    ops_count++;
    // Op 196: Enter pmap 31 va=0x58008000 pa=0x6cfdd000 prot=0x5
    if (pmaps[31]) {
        pmap_enter(pmaps[31], 1476427776, 1828573184, 5, 0);
    }
    ops_count++;
    // Op 197: Enter pmap 30 va=0x455ad000 pa=0x4e8ed000 prot=0x5
    if (pmaps[30]) {
        pmap_enter(pmaps[30], 1163579392, 1317982208, 5, 0);
    }
    ops_count++;
    // Op 198: Destroy pmap 32
    if (pmaps[32]) {
        pmap_destroy(pmaps[32]);
        pmaps[32] = 0;
    }
    ops_count++;
    // Op 199: Destroy pmap 21
    if (pmaps[21]) {
        pmap_destroy(pmaps[21]);
        pmaps[21] = 0;
    }
    ops_count++;
    kprint(".");
    // Op 200: Protect pmap 28 va=0x4094e000
    if (pmaps[28]) {
        pmap_protect(pmaps[28], 1083498496, 1083502592, 1);
    }
    ops_count++;
    // Op 201: Protect pmap 30 va=0x455ad000
    if (pmaps[30]) {
        pmap_protect(pmaps[30], 1163579392, 1163583488, 1);
    }
    ops_count++;
    // Op 202: Protect pmap 30 va=0x455ad000
    if (pmaps[30]) {
        pmap_protect(pmaps[30], 1163579392, 1163583488, 15);
    }
    ops_count++;
    // Op 203: Destroy pmap 27
    if (pmaps[27]) {
        pmap_destroy(pmaps[27]);
        pmaps[27] = 0;
    }
    ops_count++;
    // Op 204: Protect pmap 28 va=0x32c5c000
    if (pmaps[28]) {
        pmap_protect(pmaps[28], 851820544, 851824640, 1);
    }
    ops_count++;
    // Op 205: Extract pmap 31 va=0x58008000
    if (pmaps[31]) {
        pmap_extract(pmaps[31], 1476427776);
    }
    ops_count++;
    // Op 206: Remove pmap 30 va=0x455ad000
    if (pmaps[30]) {
        pmap_remove(pmaps[30], 1163579392);
    }
    ops_count++;
    // Op 207: Extract pmap 31 va=0x58008000
    if (pmaps[31]) {
        pmap_extract(pmaps[31], 1476427776);
    }
    ops_count++;
    // Op 208: Enter pmap 28 va=0xd271000 pa=0x40b26000 prot=0xf
    if (pmaps[28]) {
        pmap_enter(pmaps[28], 220663808, 1085431808, 15, 0);
    }
    ops_count++;
    // Op 209: Create pmap 33
    pmaps[33] = pmap_create();
    if (!pmaps[33]) kprint("Warning: pmap_create failed for 33\n");
    ops_count++;
    // Op 210: Create pmap 34
    pmaps[34] = pmap_create();
    if (!pmaps[34]) kprint("Warning: pmap_create failed for 34\n");
    ops_count++;
    // Op 211: Enter pmap 34 va=0xa1236000 pa=0xafcb4000 prot=0x1
    if (pmaps[34]) {
        pmap_enter(pmaps[34], 2703450112, 2949332992, 1, 0);
    }
    ops_count++;
    // Op 212: Destroy pmap 34
    if (pmaps[34]) {
        pmap_destroy(pmaps[34]);
        pmaps[34] = 0;
    }
    ops_count++;
    // Op 213: Extract pmap 28 va=0x1e52e000
    if (pmaps[28]) {
        pmap_extract(pmaps[28], 508747776);
    }
    ops_count++;
    // Op 214: Remove pmap 33 va=0x9b37b000
    if (pmaps[33]) {
        pmap_remove(pmaps[33], 2604118016);
    }
    ops_count++;
    // Op 215: Remove pmap 30 va=0x85c76000
    if (pmaps[30]) {
        pmap_remove(pmaps[30], 2244435968);
    }
    ops_count++;
    // Op 216: Enter pmap 33 va=0x4c1f6000 pa=0xdc43e000 prot=0xf
    if (pmaps[33]) {
        pmap_enter(pmaps[33], 1277124608, 3695435776, 15, 0);
    }
    ops_count++;
    // Op 217: Enter pmap 28 va=0x9c10d000 pa=0xf5d9b000 prot=0x1
    if (pmaps[28]) {
        pmap_enter(pmaps[28], 2618347520, 4124684288, 1, 0);
    }
    ops_count++;
    // Op 218: Extract pmap 30 va=0x43bfe000
    if (pmaps[30]) {
        pmap_extract(pmaps[30], 1136648192);
    }
    ops_count++;
    // Op 219: Remove pmap 30 va=0x3d67d000
    if (pmaps[30]) {
        pmap_remove(pmaps[30], 1030213632);
    }
    ops_count++;
    // Op 220: Destroy pmap 28
    if (pmaps[28]) {
        pmap_destroy(pmaps[28]);
        pmaps[28] = 0;
    }
    ops_count++;
    // Op 221: Destroy pmap 31
    if (pmaps[31]) {
        pmap_destroy(pmaps[31]);
        pmaps[31] = 0;
    }
    ops_count++;
    // Op 222: Enter pmap 33 va=0x4a900000 pa=0x86b1000 prot=0x3
    if (pmaps[33]) {
        pmap_enter(pmaps[33], 1250951168, 141234176, 3, 0);
    }
    ops_count++;
    // Op 223: Enter pmap 33 va=0xb3f70000 pa=0xdc1f2000 prot=0xf
    if (pmaps[33]) {
        pmap_enter(pmaps[33], 3019309056, 3693027328, 15, 0);
    }
    ops_count++;
    // Op 224: Create pmap 35
    pmaps[35] = pmap_create();
    if (!pmaps[35]) kprint("Warning: pmap_create failed for 35\n");
    ops_count++;
    // Op 225: Destroy pmap 33
    if (pmaps[33]) {
        pmap_destroy(pmaps[33]);
        pmaps[33] = 0;
    }
    ops_count++;
    // Op 226: Enter pmap 30 va=0xa5cb7000 pa=0x26342000 prot=0x5
    if (pmaps[30]) {
        pmap_enter(pmaps[30], 2781573120, 640950272, 5, 0);
    }
    ops_count++;
    // Op 227: Protect pmap 30 va=0xa5cb7000
    if (pmaps[30]) {
        pmap_protect(pmaps[30], 2781573120, 2781577216, 1);
    }
    ops_count++;
    // Op 228: Enter pmap 35 va=0xb04d4000 pa=0x4de8e000 prot=0xf
    if (pmaps[35]) {
        pmap_enter(pmaps[35], 2957852672, 1307107328, 15, 0);
    }
    ops_count++;
    // Op 229: Extract pmap 35 va=0xb04d4000
    if (pmaps[35]) {
        pmap_extract(pmaps[35], 2957852672);
    }
    ops_count++;
    // Op 230: Extract pmap 35 va=0xb04d4000
    if (pmaps[35]) {
        pmap_extract(pmaps[35], 2957852672);
    }
    ops_count++;
    // Op 231: Create pmap 36
    pmaps[36] = pmap_create();
    if (!pmaps[36]) kprint("Warning: pmap_create failed for 36\n");
    ops_count++;
    // Op 232: Extract pmap 36 va=0x54fa000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 89104384);
    }
    ops_count++;
    // Op 233: Extract pmap 36 va=0x9384f000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 2474962944);
    }
    ops_count++;
    // Op 234: Create pmap 37
    pmaps[37] = pmap_create();
    if (!pmaps[37]) kprint("Warning: pmap_create failed for 37\n");
    ops_count++;
    // Op 235: Enter pmap 37 va=0x47355000 pa=0x2e861000 prot=0xf
    if (pmaps[37]) {
        pmap_enter(pmaps[37], 1194676224, 780537856, 15, 0);
    }
    ops_count++;
    // Op 236: Remove pmap 37 va=0x47355000
    if (pmaps[37]) {
        pmap_remove(pmaps[37], 1194676224);
    }
    ops_count++;
    // Op 237: Enter pmap 37 va=0x5553c000 pa=0x5240d000 prot=0x1
    if (pmaps[37]) {
        pmap_enter(pmaps[37], 1431552000, 1379979264, 1, 0);
    }
    ops_count++;
    // Op 238: Extract pmap 36 va=0x7ed71000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 2128023552);
    }
    ops_count++;
    // Op 239: Enter pmap 37 va=0x8cd33000 pa=0x974f000 prot=0xf
    if (pmaps[37]) {
        pmap_enter(pmaps[37], 2362650624, 158658560, 15, 0);
    }
    ops_count++;
    // Op 240: Create pmap 38
    pmaps[38] = pmap_create();
    if (!pmaps[38]) kprint("Warning: pmap_create failed for 38\n");
    ops_count++;
    // Op 241: Enter pmap 30 va=0x67781000 pa=0xdd7ac000 prot=0x1
    if (pmaps[30]) {
        pmap_enter(pmaps[30], 1735921664, 3715809280, 1, 0);
    }
    ops_count++;
    // Op 242: Remove pmap 38 va=0x76442000
    if (pmaps[38]) {
        pmap_remove(pmaps[38], 1984176128);
    }
    ops_count++;
    // Op 243: Enter pmap 35 va=0x84b88000 pa=0x5cad9000 prot=0xf
    if (pmaps[35]) {
        pmap_enter(pmaps[35], 2226683904, 1554878464, 15, 0);
    }
    ops_count++;
    // Op 244: Remove pmap 30 va=0x67781000
    if (pmaps[30]) {
        pmap_remove(pmaps[30], 1735921664);
    }
    ops_count++;
    // Op 245: Enter pmap 35 va=0x49bc5000 pa=0x70386000 prot=0xf
    if (pmaps[35]) {
        pmap_enter(pmaps[35], 1237078016, 1882742784, 15, 0);
    }
    ops_count++;
    // Op 246: Create pmap 39
    pmaps[39] = pmap_create();
    if (!pmaps[39]) kprint("Warning: pmap_create failed for 39\n");
    ops_count++;
    // Op 247: Extract pmap 38 va=0xb5b46000
    if (pmaps[38]) {
        pmap_extract(pmaps[38], 3048497152);
    }
    ops_count++;
    // Op 248: Destroy pmap 38
    if (pmaps[38]) {
        pmap_destroy(pmaps[38]);
        pmaps[38] = 0;
    }
    ops_count++;
    // Op 249: Create pmap 40
    pmaps[40] = pmap_create();
    if (!pmaps[40]) kprint("Warning: pmap_create failed for 40\n");
    ops_count++;
    // Op 250: Enter pmap 35 va=0x1d0bd000 pa=0x76317000 prot=0x1
    if (pmaps[35]) {
        pmap_enter(pmaps[35], 487313408, 1982951424, 1, 0);
    }
    ops_count++;
    // Op 251: Remove pmap 35 va=0x1d0bd000
    if (pmaps[35]) {
        pmap_remove(pmaps[35], 487313408);
    }
    ops_count++;
    // Op 252: Extract pmap 36 va=0x45ff3000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 1174351872);
    }
    ops_count++;
    // Op 253: Enter pmap 37 va=0x78e37000 pa=0x3e752000 prot=0xf
    if (pmaps[37]) {
        pmap_enter(pmaps[37], 2028171264, 1047863296, 15, 0);
    }
    ops_count++;
    // Op 254: Remove pmap 37 va=0x5553c000
    if (pmaps[37]) {
        pmap_remove(pmaps[37], 1431552000);
    }
    ops_count++;
    // Op 255: Extract pmap 39 va=0x22f24000
    if (pmaps[39]) {
        pmap_extract(pmaps[39], 586301440);
    }
    ops_count++;
    // Op 256: Extract pmap 36 va=0x6a39b000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 1782165504);
    }
    ops_count++;
    // Op 257: Enter pmap 39 va=0x44657000 pa=0xd21f8000 prot=0x1
    if (pmaps[39]) {
        pmap_enter(pmaps[39], 1147498496, 3525279744, 1, 0);
    }
    ops_count++;
    // Op 258: Enter pmap 36 va=0x96419000 pa=0x9485d000 prot=0xf
    if (pmaps[36]) {
        pmap_enter(pmaps[36], 2520879104, 2491797504, 15, 0);
    }
    ops_count++;
    // Op 259: Extract pmap 37 va=0x78e37000
    if (pmaps[37]) {
        pmap_extract(pmaps[37], 2028171264);
    }
    ops_count++;
    // Op 260: Enter pmap 39 va=0x6090e000 pa=0x74a17000 prot=0x5
    if (pmaps[39]) {
        pmap_enter(pmaps[39], 1620107264, 1956737024, 5, 0);
    }
    ops_count++;
    // Op 261: Extract pmap 40 va=0x620a7000
    if (pmaps[40]) {
        pmap_extract(pmaps[40], 1644851200);
    }
    ops_count++;
    // Op 262: Destroy pmap 37
    if (pmaps[37]) {
        pmap_destroy(pmaps[37]);
        pmaps[37] = 0;
    }
    ops_count++;
    // Op 263: Create pmap 41
    pmaps[41] = pmap_create();
    if (!pmaps[41]) kprint("Warning: pmap_create failed for 41\n");
    ops_count++;
    // Op 264: Extract pmap 39 va=0x44657000
    if (pmaps[39]) {
        pmap_extract(pmaps[39], 1147498496);
    }
    ops_count++;
    // Op 265: Enter pmap 30 va=0x2051b000 pa=0x80a62000 prot=0x5
    if (pmaps[30]) {
        pmap_enter(pmaps[30], 542224384, 2158370816, 5, 0);
    }
    ops_count++;
    // Op 266: Extract pmap 39 va=0x6090e000
    if (pmaps[39]) {
        pmap_extract(pmaps[39], 1620107264);
    }
    ops_count++;
    // Op 267: Create pmap 42
    pmaps[42] = pmap_create();
    if (!pmaps[42]) kprint("Warning: pmap_create failed for 42\n");
    ops_count++;
    // Op 268: Create pmap 43
    pmaps[43] = pmap_create();
    if (!pmaps[43]) kprint("Warning: pmap_create failed for 43\n");
    ops_count++;
    // Op 269: Extract pmap 36 va=0x96419000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 2520879104);
    }
    ops_count++;
    // Op 270: Enter pmap 42 va=0xa65bc000 pa=0x149f8000 prot=0x5
    if (pmaps[42]) {
        pmap_enter(pmaps[42], 2791030784, 345997312, 5, 0);
    }
    ops_count++;
    // Op 271: Extract pmap 42 va=0xa075f000
    if (pmaps[42]) {
        pmap_extract(pmaps[42], 2692083712);
    }
    ops_count++;
    // Op 272: Extract pmap 30 va=0x2051b000
    if (pmaps[30]) {
        pmap_extract(pmaps[30], 542224384);
    }
    ops_count++;
    // Op 273: Remove pmap 40 va=0x3a3c9000
    if (pmaps[40]) {
        pmap_remove(pmaps[40], 977047552);
    }
    ops_count++;
    // Op 274: Protect pmap 42 va=0xa65bc000
    if (pmaps[42]) {
        pmap_protect(pmaps[42], 2791030784, 2791034880, 1);
    }
    ops_count++;
    // Op 275: Enter pmap 40 va=0x7692000 pa=0xbd61000 prot=0x5
    if (pmaps[40]) {
        pmap_enter(pmaps[40], 124329984, 198578176, 5, 0);
    }
    ops_count++;
    // Op 276: Protect pmap 40 va=0x7692000
    if (pmaps[40]) {
        pmap_protect(pmaps[40], 124329984, 124334080, 15);
    }
    ops_count++;
    // Op 277: Enter pmap 39 va=0x87fa9000 pa=0x698c3000 prot=0x3
    if (pmaps[39]) {
        pmap_enter(pmaps[39], 2281345024, 1770795008, 3, 0);
    }
    ops_count++;
    // Op 278: Destroy pmap 35
    if (pmaps[35]) {
        pmap_destroy(pmaps[35]);
        pmaps[35] = 0;
    }
    ops_count++;
    // Op 279: Remove pmap 40 va=0x7692000
    if (pmaps[40]) {
        pmap_remove(pmaps[40], 124329984);
    }
    ops_count++;
    // Op 280: Enter pmap 41 va=0x24a36000 pa=0x3b80b000 prot=0xf
    if (pmaps[41]) {
        pmap_enter(pmaps[41], 614686720, 998289408, 15, 0);
    }
    ops_count++;
    // Op 281: Remove pmap 40 va=0x415d2000
    if (pmaps[40]) {
        pmap_remove(pmaps[40], 1096622080);
    }
    ops_count++;
    // Op 282: Remove pmap 43 va=0x771ae000
    if (pmaps[43]) {
        pmap_remove(pmaps[43], 1998249984);
    }
    ops_count++;
    // Op 283: Extract pmap 42 va=0xa65bc000
    if (pmaps[42]) {
        pmap_extract(pmaps[42], 2791030784);
    }
    ops_count++;
    // Op 284: Enter pmap 39 va=0x966b2000 pa=0x4ca55000 prot=0xf
    if (pmaps[39]) {
        pmap_enter(pmaps[39], 2523602944, 1285902336, 15, 0);
    }
    ops_count++;
    // Op 285: Remove pmap 40 va=0x4d57e000
    if (pmaps[40]) {
        pmap_remove(pmaps[40], 1297604608);
    }
    ops_count++;
    // Op 286: Destroy pmap 40
    if (pmaps[40]) {
        pmap_destroy(pmaps[40]);
        pmaps[40] = 0;
    }
    ops_count++;
    // Op 287: Extract pmap 30 va=0xa5cb7000
    if (pmaps[30]) {
        pmap_extract(pmaps[30], 2781573120);
    }
    ops_count++;
    // Op 288: Remove pmap 43 va=0x4b944000
    if (pmaps[43]) {
        pmap_remove(pmaps[43], 1268006912);
    }
    ops_count++;
    // Op 289: Create pmap 44
    pmaps[44] = pmap_create();
    if (!pmaps[44]) kprint("Warning: pmap_create failed for 44\n");
    ops_count++;
    // Op 290: Protect pmap 41 va=0x24a36000
    if (pmaps[41]) {
        pmap_protect(pmaps[41], 614686720, 614690816, 1);
    }
    ops_count++;
    // Op 291: Remove pmap 43 va=0xbea2a000
    if (pmaps[43]) {
        pmap_remove(pmaps[43], 3198328832);
    }
    ops_count++;
    // Op 292: Extract pmap 42 va=0xa65bc000
    if (pmaps[42]) {
        pmap_extract(pmaps[42], 2791030784);
    }
    ops_count++;
    // Op 293: Protect pmap 36 va=0x96419000
    if (pmaps[36]) {
        pmap_protect(pmaps[36], 2520879104, 2520883200, 1);
    }
    ops_count++;
    // Op 294: Remove pmap 42 va=0xa65bc000
    if (pmaps[42]) {
        pmap_remove(pmaps[42], 2791030784);
    }
    ops_count++;
    // Op 295: Remove pmap 43 va=0xa8c02000
    if (pmaps[43]) {
        pmap_remove(pmaps[43], 2831163392);
    }
    ops_count++;
    // Op 296: Remove pmap 36 va=0x96419000
    if (pmaps[36]) {
        pmap_remove(pmaps[36], 2520879104);
    }
    ops_count++;
    // Op 297: Extract pmap 43 va=0x70d9d000
    if (pmaps[43]) {
        pmap_extract(pmaps[43], 1893322752);
    }
    ops_count++;
    // Op 298: Create pmap 45
    pmaps[45] = pmap_create();
    if (!pmaps[45]) kprint("Warning: pmap_create failed for 45\n");
    ops_count++;
    // Op 299: Enter pmap 39 va=0x17130000 pa=0xe8ed5000 prot=0x5
    if (pmaps[39]) {
        pmap_enter(pmaps[39], 387121152, 3907866624, 5, 0);
    }
    ops_count++;
    kprint(".");
    // Op 300: Enter pmap 44 va=0x2cf6c000 pa=0x33774000 prot=0x3
    if (pmaps[44]) {
        pmap_enter(pmaps[44], 754368512, 863453184, 3, 0);
    }
    ops_count++;
    // Op 301: Enter pmap 42 va=0x2a1fa000 pa=0x41d8c000 prot=0xf
    if (pmaps[42]) {
        pmap_enter(pmaps[42], 706715648, 1104723968, 15, 0);
    }
    ops_count++;
    // Op 302: Extract pmap 42 va=0x2a1fa000
    if (pmaps[42]) {
        pmap_extract(pmaps[42], 706715648);
    }
    ops_count++;
    // Op 303: Extract pmap 39 va=0x6090e000
    if (pmaps[39]) {
        pmap_extract(pmaps[39], 1620107264);
    }
    ops_count++;
    // Op 304: Extract pmap 44 va=0x8eac1000
    if (pmaps[44]) {
        pmap_extract(pmaps[44], 2393640960);
    }
    ops_count++;
    // Op 305: Enter pmap 44 va=0x3903000 pa=0x43c38000 prot=0x1
    if (pmaps[44]) {
        pmap_enter(pmaps[44], 59781120, 1136885760, 1, 0);
    }
    ops_count++;
    // Op 306: Enter pmap 42 va=0x95a5c000 pa=0x619ae000 prot=0x5
    if (pmaps[42]) {
        pmap_enter(pmaps[42], 2510667776, 1637539840, 5, 0);
    }
    ops_count++;
    // Op 307: Create pmap 46
    pmaps[46] = pmap_create();
    if (!pmaps[46]) kprint("Warning: pmap_create failed for 46\n");
    ops_count++;
    // Op 308: Destroy pmap 30
    if (pmaps[30]) {
        pmap_destroy(pmaps[30]);
        pmaps[30] = 0;
    }
    ops_count++;
    // Op 309: Remove pmap 44 va=0x2cf6c000
    if (pmaps[44]) {
        pmap_remove(pmaps[44], 754368512);
    }
    ops_count++;
    // Op 310: Remove pmap 46 va=0xb379d000
    if (pmaps[46]) {
        pmap_remove(pmaps[46], 3011104768);
    }
    ops_count++;
    // Op 311: Enter pmap 45 va=0x1dde8000 pa=0x23d86000 prot=0x1
    if (pmaps[45]) {
        pmap_enter(pmaps[45], 501121024, 601382912, 1, 0);
    }
    ops_count++;
    // Op 312: Extract pmap 43 va=0x1dba2000
    if (pmaps[43]) {
        pmap_extract(pmaps[43], 498737152);
    }
    ops_count++;
    // Op 313: Create pmap 47
    pmaps[47] = pmap_create();
    if (!pmaps[47]) kprint("Warning: pmap_create failed for 47\n");
    ops_count++;
    // Op 314: Extract pmap 41 va=0x24a36000
    if (pmaps[41]) {
        pmap_extract(pmaps[41], 614686720);
    }
    ops_count++;
    // Op 315: Remove pmap 47 va=0x6b4bf000
    if (pmaps[47]) {
        pmap_remove(pmaps[47], 1800138752);
    }
    ops_count++;
    // Op 316: Remove pmap 41 va=0x24a36000
    if (pmaps[41]) {
        pmap_remove(pmaps[41], 614686720);
    }
    ops_count++;
    // Op 317: Remove pmap 46 va=0x9d9b7000
    if (pmaps[46]) {
        pmap_remove(pmaps[46], 2644209664);
    }
    ops_count++;
    // Op 318: Enter pmap 43 va=0x8602000 pa=0xb0c63000 prot=0x5
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 140517376, 2965778432, 5, 0);
    }
    ops_count++;
    // Op 319: Destroy pmap 46
    if (pmaps[46]) {
        pmap_destroy(pmaps[46]);
        pmaps[46] = 0;
    }
    ops_count++;
    // Op 320: Extract pmap 44 va=0x3903000
    if (pmaps[44]) {
        pmap_extract(pmaps[44], 59781120);
    }
    ops_count++;
    // Op 321: Enter pmap 44 va=0xf804000 pa=0x65fc7000 prot=0x5
    if (pmaps[44]) {
        pmap_enter(pmaps[44], 260063232, 1711042560, 5, 0);
    }
    ops_count++;
    // Op 322: Destroy pmap 39
    if (pmaps[39]) {
        pmap_destroy(pmaps[39]);
        pmaps[39] = 0;
    }
    ops_count++;
    // Op 323: Extract pmap 47 va=0xa9ab4000
    if (pmaps[47]) {
        pmap_extract(pmaps[47], 2846572544);
    }
    ops_count++;
    // Op 324: Destroy pmap 45
    if (pmaps[45]) {
        pmap_destroy(pmaps[45]);
        pmaps[45] = 0;
    }
    ops_count++;
    // Op 325: Remove pmap 36 va=0xcf2c000
    if (pmaps[36]) {
        pmap_remove(pmaps[36], 217235456);
    }
    ops_count++;
    // Op 326: Extract pmap 44 va=0xf804000
    if (pmaps[44]) {
        pmap_extract(pmaps[44], 260063232);
    }
    ops_count++;
    // Op 327: Remove pmap 41 va=0x541c8000
    if (pmaps[41]) {
        pmap_remove(pmaps[41], 1411153920);
    }
    ops_count++;
    // Op 328: Protect pmap 44 va=0xf804000
    if (pmaps[44]) {
        pmap_protect(pmaps[44], 260063232, 260067328, 15);
    }
    ops_count++;
    // Op 329: Extract pmap 41 va=0x8a4b9000
    if (pmaps[41]) {
        pmap_extract(pmaps[41], 2320207872);
    }
    ops_count++;
    // Op 330: Enter pmap 41 va=0x1c243000 pa=0xa94b1000 prot=0x1
    if (pmaps[41]) {
        pmap_enter(pmaps[41], 472133632, 2840268800, 1, 0);
    }
    ops_count++;
    // Op 331: Create pmap 48
    pmaps[48] = pmap_create();
    if (!pmaps[48]) kprint("Warning: pmap_create failed for 48\n");
    ops_count++;
    // Op 332: Enter pmap 48 va=0x3ce92000 pa=0x96cf1000 prot=0x5
    if (pmaps[48]) {
        pmap_enter(pmaps[48], 1021911040, 2530152448, 5, 0);
    }
    ops_count++;
    // Op 333: Create pmap 49
    pmaps[49] = pmap_create();
    if (!pmaps[49]) kprint("Warning: pmap_create failed for 49\n");
    ops_count++;
    // Op 334: Enter pmap 42 va=0xbde14000 pa=0x6bd41000 prot=0x1
    if (pmaps[42]) {
        pmap_enter(pmaps[42], 3185655808, 1809059840, 1, 0);
    }
    ops_count++;
    // Op 335: Enter pmap 47 va=0x8363a000 pa=0x980a7000 prot=0x1
    if (pmaps[47]) {
        pmap_enter(pmaps[47], 2204344320, 2550820864, 1, 0);
    }
    ops_count++;
    // Op 336: Enter pmap 43 va=0x9d77b000 pa=0xb29f000 prot=0x5
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 2641866752, 187297792, 5, 0);
    }
    ops_count++;
    // Op 337: Enter pmap 36 va=0xf925000 pa=0xffb36000 prot=0xf
    if (pmaps[36]) {
        pmap_enter(pmaps[36], 261246976, 4289945600, 15, 0);
    }
    ops_count++;
    // Op 338: Protect pmap 48 va=0x3ce92000
    if (pmaps[48]) {
        pmap_protect(pmaps[48], 1021911040, 1021915136, 15);
    }
    ops_count++;
    // Op 339: Create pmap 50
    pmaps[50] = pmap_create();
    if (!pmaps[50]) kprint("Warning: pmap_create failed for 50\n");
    ops_count++;
    // Op 340: Create pmap 51
    pmaps[51] = pmap_create();
    if (!pmaps[51]) kprint("Warning: pmap_create failed for 51\n");
    ops_count++;
    // Op 341: Remove pmap 41 va=0x1c243000
    if (pmaps[41]) {
        pmap_remove(pmaps[41], 472133632);
    }
    ops_count++;
    // Op 342: Enter pmap 51 va=0x8c612000 pa=0xb65fa000 prot=0x5
    if (pmaps[51]) {
        pmap_enter(pmaps[51], 2355175424, 3059720192, 5, 0);
    }
    ops_count++;
    // Op 343: Enter pmap 51 va=0x87d2a000 pa=0x4b8e6000 prot=0xf
    if (pmaps[51]) {
        pmap_enter(pmaps[51], 2278727680, 1267621888, 15, 0);
    }
    ops_count++;
    // Op 344: Enter pmap 48 va=0x1963d000 pa=0xcb185000 prot=0x1
    if (pmaps[48]) {
        pmap_enter(pmaps[48], 425971712, 3407368192, 1, 0);
    }
    ops_count++;
    // Op 345: Extract pmap 50 va=0x370bd000
    if (pmaps[50]) {
        pmap_extract(pmaps[50], 923521024);
    }
    ops_count++;
    // Op 346: Enter pmap 43 va=0x69efb000 pa=0x56d43000 prot=0xf
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 1777315840, 1456746496, 15, 0);
    }
    ops_count++;
    // Op 347: Enter pmap 41 va=0x50033000 pa=0x6d506000 prot=0x5
    if (pmaps[41]) {
        pmap_enter(pmaps[41], 1342386176, 1833984000, 5, 0);
    }
    ops_count++;
    // Op 348: Remove pmap 47 va=0x8363a000
    if (pmaps[47]) {
        pmap_remove(pmaps[47], 2204344320);
    }
    ops_count++;
    // Op 349: Remove pmap 49 va=0x112fb000
    if (pmaps[49]) {
        pmap_remove(pmaps[49], 288337920);
    }
    ops_count++;
    // Op 350: Create pmap 52
    pmaps[52] = pmap_create();
    if (!pmaps[52]) kprint("Warning: pmap_create failed for 52\n");
    ops_count++;
    // Op 351: Create pmap 53
    pmaps[53] = pmap_create();
    if (!pmaps[53]) kprint("Warning: pmap_create failed for 53\n");
    ops_count++;
    // Op 352: Enter pmap 53 va=0xbd21c000 pa=0x5f75c000 prot=0x3
    if (pmaps[53]) {
        pmap_enter(pmaps[53], 3173105664, 1601552384, 3, 0);
    }
    ops_count++;
    // Op 353: Remove pmap 51 va=0x87d2a000
    if (pmaps[51]) {
        pmap_remove(pmaps[51], 2278727680);
    }
    ops_count++;
    // Op 354: Remove pmap 48 va=0x1963d000
    if (pmaps[48]) {
        pmap_remove(pmaps[48], 425971712);
    }
    ops_count++;
    // Op 355: Extract pmap 48 va=0xb8977000
    if (pmaps[48]) {
        pmap_extract(pmaps[48], 3096932352);
    }
    ops_count++;
    // Op 356: Create pmap 54
    pmaps[54] = pmap_create();
    if (!pmaps[54]) kprint("Warning: pmap_create failed for 54\n");
    ops_count++;
    // Op 357: Enter pmap 44 va=0x5a058000 pa=0x1a959000 prot=0x3
    if (pmaps[44]) {
        pmap_enter(pmaps[44], 1510309888, 446009344, 3, 0);
    }
    ops_count++;
    // Op 358: Destroy pmap 49
    if (pmaps[49]) {
        pmap_destroy(pmaps[49]);
        pmaps[49] = 0;
    }
    ops_count++;
    // Op 359: Destroy pmap 41
    if (pmaps[41]) {
        pmap_destroy(pmaps[41]);
        pmaps[41] = 0;
    }
    ops_count++;
    // Op 360: Enter pmap 52 va=0x5e188000 pa=0x1d791000 prot=0x5
    if (pmaps[52]) {
        pmap_enter(pmaps[52], 1578663936, 494473216, 5, 0);
    }
    ops_count++;
    // Op 361: Remove pmap 50 va=0x8f9b8000
    if (pmaps[50]) {
        pmap_remove(pmaps[50], 2409332736);
    }
    ops_count++;
    // Op 362: Extract pmap 53 va=0xbd21c000
    if (pmaps[53]) {
        pmap_extract(pmaps[53], 3173105664);
    }
    ops_count++;
    // Op 363: Remove pmap 54 va=0xb1876000
    if (pmaps[54]) {
        pmap_remove(pmaps[54], 2978439168);
    }
    ops_count++;
    // Op 364: Enter pmap 43 va=0x45f51000 pa=0xb3f41000 prot=0x5
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 1173688320, 3019116544, 5, 0);
    }
    ops_count++;
    // Op 365: Extract pmap 48 va=0x3ce92000
    if (pmaps[48]) {
        pmap_extract(pmaps[48], 1021911040);
    }
    ops_count++;
    // Op 366: Remove pmap 50 va=0x11d06000
    if (pmaps[50]) {
        pmap_remove(pmaps[50], 298868736);
    }
    ops_count++;
    // Op 367: Create pmap 55
    pmaps[55] = pmap_create();
    if (!pmaps[55]) kprint("Warning: pmap_create failed for 55\n");
    ops_count++;
    // Op 368: Remove pmap 36 va=0xf925000
    if (pmaps[36]) {
        pmap_remove(pmaps[36], 261246976);
    }
    ops_count++;
    // Op 369: Protect pmap 44 va=0x3903000
    if (pmaps[44]) {
        pmap_protect(pmaps[44], 59781120, 59785216, 15);
    }
    ops_count++;
    // Op 370: Enter pmap 43 va=0x5ebbd000 pa=0x4fd77000 prot=0x5
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 1589366784, 1339518976, 5, 0);
    }
    ops_count++;
    // Op 371: Protect pmap 53 va=0xbd21c000
    if (pmaps[53]) {
        pmap_protect(pmaps[53], 3173105664, 3173109760, 1);
    }
    ops_count++;
    // Op 372: Destroy pmap 53
    if (pmaps[53]) {
        pmap_destroy(pmaps[53]);
        pmaps[53] = 0;
    }
    ops_count++;
    // Op 373: Create pmap 56
    pmaps[56] = pmap_create();
    if (!pmaps[56]) kprint("Warning: pmap_create failed for 56\n");
    ops_count++;
    // Op 374: Create pmap 57
    pmaps[57] = pmap_create();
    if (!pmaps[57]) kprint("Warning: pmap_create failed for 57\n");
    ops_count++;
    // Op 375: Enter pmap 50 va=0x7c546000 pa=0x9b795000 prot=0xf
    if (pmaps[50]) {
        pmap_enter(pmaps[50], 2085904384, 2608418816, 15, 0);
    }
    ops_count++;
    // Op 376: Enter pmap 44 va=0x83293000 pa=0x1d332000 prot=0x5
    if (pmaps[44]) {
        pmap_enter(pmaps[44], 2200514560, 489889792, 5, 0);
    }
    ops_count++;
    // Op 377: Enter pmap 47 va=0xad9fc000 pa=0xadc14000 prot=0xf
    if (pmaps[47]) {
        pmap_enter(pmaps[47], 2912927744, 2915123200, 15, 0);
    }
    ops_count++;
    // Op 378: Enter pmap 47 va=0xba08000 pa=0x3883e000 prot=0xf
    if (pmaps[47]) {
        pmap_enter(pmaps[47], 195067904, 948166656, 15, 0);
    }
    ops_count++;
    // Op 379: Extract pmap 36 va=0x4d29e000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 1294589952);
    }
    ops_count++;
    // Op 380: Extract pmap 57 va=0x4169c000
    if (pmaps[57]) {
        pmap_extract(pmaps[57], 1097449472);
    }
    ops_count++;
    // Op 381: Enter pmap 42 va=0x1faa000 pa=0x7f64a000 prot=0xf
    if (pmaps[42]) {
        pmap_enter(pmaps[42], 33202176, 2137300992, 15, 0);
    }
    ops_count++;
    // Op 382: Destroy pmap 50
    if (pmaps[50]) {
        pmap_destroy(pmaps[50]);
        pmaps[50] = 0;
    }
    ops_count++;
    // Op 383: Enter pmap 44 va=0x80114000 pa=0x8f188000 prot=0x5
    if (pmaps[44]) {
        pmap_enter(pmaps[44], 2148614144, 2400747520, 5, 0);
    }
    ops_count++;
    // Op 384: Create pmap 58
    pmaps[58] = pmap_create();
    if (!pmaps[58]) kprint("Warning: pmap_create failed for 58\n");
    ops_count++;
    // Op 385: Extract pmap 36 va=0x75b28000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 1974632448);
    }
    ops_count++;
    // Op 386: Extract pmap 48 va=0x3ce92000
    if (pmaps[48]) {
        pmap_extract(pmaps[48], 1021911040);
    }
    ops_count++;
    // Op 387: Protect pmap 51 va=0x8c612000
    if (pmaps[51]) {
        pmap_protect(pmaps[51], 2355175424, 2355179520, 1);
    }
    ops_count++;
    // Op 388: Enter pmap 48 va=0x2bfff000 pa=0xcd337000 prot=0xf
    if (pmaps[48]) {
        pmap_enter(pmaps[48], 738193408, 3442700288, 15, 0);
    }
    ops_count++;
    // Op 389: Protect pmap 48 va=0x3ce92000
    if (pmaps[48]) {
        pmap_protect(pmaps[48], 1021911040, 1021915136, 15);
    }
    ops_count++;
    // Op 390: Protect pmap 44 va=0x5a058000
    if (pmaps[44]) {
        pmap_protect(pmaps[44], 1510309888, 1510313984, 15);
    }
    ops_count++;
    // Op 391: Enter pmap 51 va=0x4f6e3000 pa=0xbefd1000 prot=0x5
    if (pmaps[51]) {
        pmap_enter(pmaps[51], 1332621312, 3204255744, 5, 0);
    }
    ops_count++;
    // Op 392: Destroy pmap 58
    if (pmaps[58]) {
        pmap_destroy(pmaps[58]);
        pmaps[58] = 0;
    }
    ops_count++;
    // Op 393: Destroy pmap 54
    if (pmaps[54]) {
        pmap_destroy(pmaps[54]);
        pmaps[54] = 0;
    }
    ops_count++;
    // Op 394: Remove pmap 55 va=0x828c4000
    if (pmaps[55]) {
        pmap_remove(pmaps[55], 2190229504);
    }
    ops_count++;
    // Op 395: Destroy pmap 48
    if (pmaps[48]) {
        pmap_destroy(pmaps[48]);
        pmaps[48] = 0;
    }
    ops_count++;
    // Op 396: Enter pmap 43 va=0x3c7c2000 pa=0x1a616000 prot=0x3
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 1014767616, 442589184, 3, 0);
    }
    ops_count++;
    // Op 397: Enter pmap 43 va=0x9a36e000 pa=0x27327000 prot=0x1
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 2587287552, 657616896, 1, 0);
    }
    ops_count++;
    // Op 398: Destroy pmap 55
    if (pmaps[55]) {
        pmap_destroy(pmaps[55]);
        pmaps[55] = 0;
    }
    ops_count++;
    // Op 399: Enter pmap 56 va=0xae5ed000 pa=0xeca55000 prot=0x5
    if (pmaps[56]) {
        pmap_enter(pmaps[56], 2925449216, 3970256896, 5, 0);
    }
    ops_count++;
    kprint(".");
    // Op 400: Extract pmap 51 va=0x8c612000
    if (pmaps[51]) {
        pmap_extract(pmaps[51], 2355175424);
    }
    ops_count++;
    // Op 401: Enter pmap 47 va=0x46511000 pa=0x97774000 prot=0x1
    if (pmaps[47]) {
        pmap_enter(pmaps[47], 1179717632, 2541174784, 1, 0);
    }
    ops_count++;
    // Op 402: Enter pmap 42 va=0x4f76f000 pa=0x7644c000 prot=0xf
    if (pmaps[42]) {
        pmap_enter(pmaps[42], 1333194752, 1984217088, 15, 0);
    }
    ops_count++;
    // Op 403: Create pmap 59
    pmaps[59] = pmap_create();
    if (!pmaps[59]) kprint("Warning: pmap_create failed for 59\n");
    ops_count++;
    // Op 404: Enter pmap 47 va=0x13a4b000 pa=0xa518d000 prot=0x1
    if (pmaps[47]) {
        pmap_enter(pmaps[47], 329560064, 2769866752, 1, 0);
    }
    ops_count++;
    // Op 405: Remove pmap 57 va=0x626d8000
    if (pmaps[57]) {
        pmap_remove(pmaps[57], 1651343360);
    }
    ops_count++;
    // Op 406: Enter pmap 57 va=0xbd29f000 pa=0xe5682000 prot=0x1
    if (pmaps[57]) {
        pmap_enter(pmaps[57], 3173642240, 3848806400, 1, 0);
    }
    ops_count++;
    // Op 407: Enter pmap 59 va=0xa6dee000 pa=0x30407000 prot=0x5
    if (pmaps[59]) {
        pmap_enter(pmaps[59], 2799624192, 809529344, 5, 0);
    }
    ops_count++;
    // Op 408: Remove pmap 57 va=0xbd29f000
    if (pmaps[57]) {
        pmap_remove(pmaps[57], 3173642240);
    }
    ops_count++;
    // Op 409: Extract pmap 56 va=0xae5ed000
    if (pmaps[56]) {
        pmap_extract(pmaps[56], 2925449216);
    }
    ops_count++;
    // Op 410: Extract pmap 42 va=0x95a5c000
    if (pmaps[42]) {
        pmap_extract(pmaps[42], 2510667776);
    }
    ops_count++;
    // Op 411: Create pmap 60
    pmaps[60] = pmap_create();
    if (!pmaps[60]) kprint("Warning: pmap_create failed for 60\n");
    ops_count++;
    // Op 412: Protect pmap 56 va=0xae5ed000
    if (pmaps[56]) {
        pmap_protect(pmaps[56], 2925449216, 2925453312, 15);
    }
    ops_count++;
    // Op 413: Enter pmap 47 va=0x632dc000 pa=0x68b51000 prot=0x5
    if (pmaps[47]) {
        pmap_enter(pmaps[47], 1663942656, 1756696576, 5, 0);
    }
    ops_count++;
    // Op 414: Remove pmap 36 va=0xa181d000
    if (pmaps[36]) {
        pmap_remove(pmaps[36], 2709639168);
    }
    ops_count++;
    // Op 415: Remove pmap 42 va=0x4f76f000
    if (pmaps[42]) {
        pmap_remove(pmaps[42], 1333194752);
    }
    ops_count++;
    // Op 416: Create pmap 61
    pmaps[61] = pmap_create();
    if (!pmaps[61]) kprint("Warning: pmap_create failed for 61\n");
    ops_count++;
    // Op 417: Remove pmap 47 va=0x46511000
    if (pmaps[47]) {
        pmap_remove(pmaps[47], 1179717632);
    }
    ops_count++;
    // Op 418: Extract pmap 59 va=0x55574000
    if (pmaps[59]) {
        pmap_extract(pmaps[59], 1431781376);
    }
    ops_count++;
    // Op 419: Create pmap 62
    pmaps[62] = pmap_create();
    if (!pmaps[62]) kprint("Warning: pmap_create failed for 62\n");
    ops_count++;
    // Op 420: Remove pmap 51 va=0x4f6e3000
    if (pmaps[51]) {
        pmap_remove(pmaps[51], 1332621312);
    }
    ops_count++;
    // Op 421: Extract pmap 61 va=0x21030000
    if (pmaps[61]) {
        pmap_extract(pmaps[61], 553844736);
    }
    ops_count++;
    // Op 422: Remove pmap 42 va=0x1faa000
    if (pmaps[42]) {
        pmap_remove(pmaps[42], 33202176);
    }
    ops_count++;
    // Op 423: Remove pmap 60 va=0x54171000
    if (pmaps[60]) {
        pmap_remove(pmaps[60], 1410797568);
    }
    ops_count++;
    // Op 424: Extract pmap 57 va=0xaba31000
    if (pmaps[57]) {
        pmap_extract(pmaps[57], 2879590400);
    }
    ops_count++;
    // Op 425: Enter pmap 51 va=0x4aad000 pa=0x5ce33000 prot=0x5
    if (pmaps[51]) {
        pmap_enter(pmaps[51], 78303232, 1558392832, 5, 0);
    }
    ops_count++;
    // Op 426: Destroy pmap 44
    if (pmaps[44]) {
        pmap_destroy(pmaps[44]);
        pmaps[44] = 0;
    }
    ops_count++;
    // Op 427: Enter pmap 57 va=0x31275000 pa=0x3a0eb000 prot=0x3
    if (pmaps[57]) {
        pmap_enter(pmaps[57], 824659968, 974041088, 3, 0);
    }
    ops_count++;
    // Op 428: Destroy pmap 51
    if (pmaps[51]) {
        pmap_destroy(pmaps[51]);
        pmaps[51] = 0;
    }
    ops_count++;
    // Op 429: Protect pmap 42 va=0xbde14000
    if (pmaps[42]) {
        pmap_protect(pmaps[42], 3185655808, 3185659904, 1);
    }
    ops_count++;
    // Op 430: Remove pmap 61 va=0x218c7000
    if (pmaps[61]) {
        pmap_remove(pmaps[61], 562851840);
    }
    ops_count++;
    // Op 431: Remove pmap 43 va=0x9d77b000
    if (pmaps[43]) {
        pmap_remove(pmaps[43], 2641866752);
    }
    ops_count++;
    // Op 432: Destroy pmap 61
    if (pmaps[61]) {
        pmap_destroy(pmaps[61]);
        pmaps[61] = 0;
    }
    ops_count++;
    // Op 433: Protect pmap 43 va=0x9a36e000
    if (pmaps[43]) {
        pmap_protect(pmaps[43], 2587287552, 2587291648, 15);
    }
    ops_count++;
    // Op 434: Create pmap 63
    pmaps[63] = pmap_create();
    if (!pmaps[63]) kprint("Warning: pmap_create failed for 63\n");
    ops_count++;
    // Op 435: Enter pmap 47 va=0x71b71000 pa=0x9c5ff000 prot=0x5
    if (pmaps[47]) {
        pmap_enter(pmaps[47], 1907822592, 2623533056, 5, 0);
    }
    ops_count++;
    // Op 436: Protect pmap 59 va=0xa6dee000
    if (pmaps[59]) {
        pmap_protect(pmaps[59], 2799624192, 2799628288, 1);
    }
    ops_count++;
    // Op 437: Enter pmap 59 va=0x31ab1000 pa=0x5e3ad000 prot=0xf
    if (pmaps[59]) {
        pmap_enter(pmaps[59], 833294336, 1580912640, 15, 0);
    }
    ops_count++;
    // Op 438: Enter pmap 52 va=0x61c27000 pa=0x80ce7000 prot=0xf
    if (pmaps[52]) {
        pmap_enter(pmaps[52], 1640132608, 2161012736, 15, 0);
    }
    ops_count++;
    // Op 439: Extract pmap 47 va=0x236e2000
    if (pmaps[47]) {
        pmap_extract(pmaps[47], 594419712);
    }
    ops_count++;
    // Op 440: Extract pmap 36 va=0x5f07d000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 1594347520);
    }
    ops_count++;
    // Op 441: Remove pmap 42 va=0xbde14000
    if (pmaps[42]) {
        pmap_remove(pmaps[42], 3185655808);
    }
    ops_count++;
    // Op 442: Protect pmap 42 va=0x95a5c000
    if (pmaps[42]) {
        pmap_protect(pmaps[42], 2510667776, 2510671872, 1);
    }
    ops_count++;
    // Op 443: Protect pmap 52 va=0x61c27000
    if (pmaps[52]) {
        pmap_protect(pmaps[52], 1640132608, 1640136704, 1);
    }
    ops_count++;
    // Op 444: Protect pmap 42 va=0x2a1fa000
    if (pmaps[42]) {
        pmap_protect(pmaps[42], 706715648, 706719744, 15);
    }
    ops_count++;
    // Op 445: Extract pmap 36 va=0x657e1000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 1702760448);
    }
    ops_count++;
    // Op 446: Enter pmap 47 va=0x62dbd000 pa=0xfea93000 prot=0x1
    if (pmaps[47]) {
        pmap_enter(pmaps[47], 1658572800, 4272500736, 1, 0);
    }
    ops_count++;
    // Op 447: Enter pmap 36 va=0x51956000 pa=0xee5a9000 prot=0x1
    if (pmaps[36]) {
        pmap_enter(pmaps[36], 1368743936, 3998912512, 1, 0);
    }
    ops_count++;
    // Op 448: Enter pmap 43 va=0x2339c000 pa=0x9de3000 prot=0x5
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 590987264, 165556224, 5, 0);
    }
    ops_count++;
    // Op 449: Extract pmap 59 va=0xa6dee000
    if (pmaps[59]) {
        pmap_extract(pmaps[59], 2799624192);
    }
    ops_count++;
    // Op 450: Protect pmap 59 va=0x31ab1000
    if (pmaps[59]) {
        pmap_protect(pmaps[59], 833294336, 833298432, 1);
    }
    ops_count++;
    // Op 451: Extract pmap 36 va=0x51956000
    if (pmaps[36]) {
        pmap_extract(pmaps[36], 1368743936);
    }
    ops_count++;
    // Op 452: Enter pmap 62 va=0x871c1000 pa=0x6c6a6000 prot=0x1
    if (pmaps[62]) {
        pmap_enter(pmaps[62], 2266763264, 1818910720, 1, 0);
    }
    ops_count++;
    // Op 453: Protect pmap 47 va=0x71b71000
    if (pmaps[47]) {
        pmap_protect(pmaps[47], 1907822592, 1907826688, 1);
    }
    ops_count++;
    // Op 454: Create pmap 64
    pmaps[64] = pmap_create();
    if (!pmaps[64]) kprint("Warning: pmap_create failed for 64\n");
    ops_count++;
    // Op 455: Enter pmap 62 va=0x75057000 pa=0x10242000 prot=0x1
    if (pmaps[62]) {
        pmap_enter(pmaps[62], 1963290624, 270802944, 1, 0);
    }
    ops_count++;
    // Op 456: Protect pmap 59 va=0xa6dee000
    if (pmaps[59]) {
        pmap_protect(pmaps[59], 2799624192, 2799628288, 1);
    }
    ops_count++;
    // Op 457: Protect pmap 52 va=0x61c27000
    if (pmaps[52]) {
        pmap_protect(pmaps[52], 1640132608, 1640136704, 1);
    }
    ops_count++;
    // Op 458: Remove pmap 47 va=0x632dc000
    if (pmaps[47]) {
        pmap_remove(pmaps[47], 1663942656);
    }
    ops_count++;
    // Op 459: Enter pmap 63 va=0xab103000 pa=0x15fac000 prot=0x5
    if (pmaps[63]) {
        pmap_enter(pmaps[63], 2869964800, 368754688, 5, 0);
    }
    ops_count++;
    // Op 460: Create pmap 65
    pmaps[65] = pmap_create();
    if (!pmaps[65]) kprint("Warning: pmap_create failed for 65\n");
    ops_count++;
    // Op 461: Enter pmap 60 va=0x81f39000 pa=0x8dece000 prot=0x1
    if (pmaps[60]) {
        pmap_enter(pmaps[60], 2180222976, 2381111296, 1, 0);
    }
    ops_count++;
    // Op 462: Enter pmap 59 va=0xb252000 pa=0xa2cb5000 prot=0xf
    if (pmaps[59]) {
        pmap_enter(pmaps[59], 186982400, 2731233280, 15, 0);
    }
    ops_count++;
    // Op 463: Extract pmap 52 va=0x61c27000
    if (pmaps[52]) {
        pmap_extract(pmaps[52], 1640132608);
    }
    ops_count++;
    // Op 464: Protect pmap 56 va=0xae5ed000
    if (pmaps[56]) {
        pmap_protect(pmaps[56], 2925449216, 2925453312, 1);
    }
    ops_count++;
    // Op 465: Protect pmap 43 va=0x8602000
    if (pmaps[43]) {
        pmap_protect(pmaps[43], 140517376, 140521472, 15);
    }
    ops_count++;
    // Op 466: Enter pmap 63 va=0x2cd2b000 pa=0xd4a40000 prot=0xf
    if (pmaps[63]) {
        pmap_enter(pmaps[63], 752005120, 3567517696, 15, 0);
    }
    ops_count++;
    // Op 467: Extract pmap 59 va=0xa6dee000
    if (pmaps[59]) {
        pmap_extract(pmaps[59], 2799624192);
    }
    ops_count++;
    // Op 468: Create pmap 66
    pmaps[66] = pmap_create();
    if (!pmaps[66]) kprint("Warning: pmap_create failed for 66\n");
    ops_count++;
    // Op 469: Protect pmap 59 va=0xa6dee000
    if (pmaps[59]) {
        pmap_protect(pmaps[59], 2799624192, 2799628288, 15);
    }
    ops_count++;
    // Op 470: Destroy pmap 65
    if (pmaps[65]) {
        pmap_destroy(pmaps[65]);
        pmaps[65] = 0;
    }
    ops_count++;
    // Op 471: Destroy pmap 36
    if (pmaps[36]) {
        pmap_destroy(pmaps[36]);
        pmaps[36] = 0;
    }
    ops_count++;
    // Op 472: Enter pmap 56 va=0x83f03000 pa=0x66020000 prot=0xf
    if (pmaps[56]) {
        pmap_enter(pmaps[56], 2213556224, 1711407104, 15, 0);
    }
    ops_count++;
    // Op 473: Enter pmap 64 va=0x30ec3000 pa=0x494e9000 prot=0x5
    if (pmaps[64]) {
        pmap_enter(pmaps[64], 820785152, 1229885440, 5, 0);
    }
    ops_count++;
    // Op 474: Extract pmap 42 va=0x54f91000
    if (pmaps[42]) {
        pmap_extract(pmaps[42], 1425608704);
    }
    ops_count++;
    // Op 475: Enter pmap 57 va=0x6fdc1000 pa=0xe3c56000 prot=0xf
    if (pmaps[57]) {
        pmap_enter(pmaps[57], 1876692992, 3821363200, 15, 0);
    }
    ops_count++;
    // Op 476: Protect pmap 59 va=0x31ab1000
    if (pmaps[59]) {
        pmap_protect(pmaps[59], 833294336, 833298432, 1);
    }
    ops_count++;
    // Op 477: Enter pmap 60 va=0x5e0a8000 pa=0xecaf9000 prot=0x5
    if (pmaps[60]) {
        pmap_enter(pmaps[60], 1577746432, 3970928640, 5, 0);
    }
    ops_count++;
    // Op 478: Enter pmap 59 va=0x9a3a7000 pa=0xfecec000 prot=0x3
    if (pmaps[59]) {
        pmap_enter(pmaps[59], 2587521024, 4274962432, 3, 0);
    }
    ops_count++;
    // Op 479: Enter pmap 57 va=0x1a43e000 pa=0x148f6000 prot=0x5
    if (pmaps[57]) {
        pmap_enter(pmaps[57], 440655872, 344940544, 5, 0);
    }
    ops_count++;
    // Op 480: Remove pmap 56 va=0x83f03000
    if (pmaps[56]) {
        pmap_remove(pmaps[56], 2213556224);
    }
    ops_count++;
    // Op 481: Remove pmap 59 va=0x31ab1000
    if (pmaps[59]) {
        pmap_remove(pmaps[59], 833294336);
    }
    ops_count++;
    // Op 482: Remove pmap 57 va=0x6fdc1000
    if (pmaps[57]) {
        pmap_remove(pmaps[57], 1876692992);
    }
    ops_count++;
    // Op 483: Create pmap 67
    pmaps[67] = pmap_create();
    if (!pmaps[67]) kprint("Warning: pmap_create failed for 67\n");
    ops_count++;
    // Op 484: Extract pmap 57 va=0x1a43e000
    if (pmaps[57]) {
        pmap_extract(pmaps[57], 440655872);
    }
    ops_count++;
    // Op 485: Enter pmap 67 va=0xeaf1000 pa=0x1345e000 prot=0xf
    if (pmaps[67]) {
        pmap_enter(pmaps[67], 246353920, 323346432, 15, 0);
    }
    ops_count++;
    // Op 486: Enter pmap 67 va=0xbff96000 pa=0xadfdd000 prot=0x3
    if (pmaps[67]) {
        pmap_enter(pmaps[67], 3220791296, 2919092224, 3, 0);
    }
    ops_count++;
    // Op 487: Extract pmap 47 va=0xad974000
    if (pmaps[47]) {
        pmap_extract(pmaps[47], 2912370688);
    }
    ops_count++;
    // Op 488: Protect pmap 42 va=0x2a1fa000
    if (pmaps[42]) {
        pmap_protect(pmaps[42], 706715648, 706719744, 1);
    }
    ops_count++;
    // Op 489: Destroy pmap 64
    if (pmaps[64]) {
        pmap_destroy(pmaps[64]);
        pmaps[64] = 0;
    }
    ops_count++;
    // Op 490: Enter pmap 59 va=0x91370000 pa=0x85a6000 prot=0x3
    if (pmaps[59]) {
        pmap_enter(pmaps[59], 2436300800, 140140544, 3, 0);
    }
    ops_count++;
    // Op 491: Remove pmap 57 va=0x1a43e000
    if (pmaps[57]) {
        pmap_remove(pmaps[57], 440655872);
    }
    ops_count++;
    // Op 492: Enter pmap 43 va=0x92ec9000 pa=0x234ff000 prot=0x5
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 2464976896, 592441344, 5, 0);
    }
    ops_count++;
    // Op 493: Enter pmap 66 va=0x47584000 pa=0x3ff68000 prot=0x1
    if (pmaps[66]) {
        pmap_enter(pmaps[66], 1196965888, 1073119232, 1, 0);
    }
    ops_count++;
    // Op 494: Create pmap 68
    pmaps[68] = pmap_create();
    if (!pmaps[68]) kprint("Warning: pmap_create failed for 68\n");
    ops_count++;
    // Op 495: Destroy pmap 62
    if (pmaps[62]) {
        pmap_destroy(pmaps[62]);
        pmaps[62] = 0;
    }
    ops_count++;
    // Op 496: Enter pmap 63 va=0x1e25a000 pa=0x4313c000 prot=0x5
    if (pmaps[63]) {
        pmap_enter(pmaps[63], 505782272, 1125367808, 5, 0);
    }
    ops_count++;
    // Op 497: Protect pmap 52 va=0x61c27000
    if (pmaps[52]) {
        pmap_protect(pmaps[52], 1640132608, 1640136704, 15);
    }
    ops_count++;
    // Op 498: Destroy pmap 47
    if (pmaps[47]) {
        pmap_destroy(pmaps[47]);
        pmaps[47] = 0;
    }
    ops_count++;
    // Op 499: Extract pmap 63 va=0x1e25a000
    if (pmaps[63]) {
        pmap_extract(pmaps[63], 505782272);
    }
    ops_count++;
    kprint(".");
    // Op 500: Enter pmap 43 va=0xae92f000 pa=0xf7c21000 prot=0x5
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 2928865280, 4156690432, 5, 0);
    }
    ops_count++;
    // Op 501: Remove pmap 43 va=0xae92f000
    if (pmaps[43]) {
        pmap_remove(pmaps[43], 2928865280);
    }
    ops_count++;
    // Op 502: Enter pmap 57 va=0x28525000 pa=0xb6491000 prot=0x3
    if (pmaps[57]) {
        pmap_enter(pmaps[57], 676483072, 3058241536, 3, 0);
    }
    ops_count++;
    // Op 503: Protect pmap 66 va=0x47584000
    if (pmaps[66]) {
        pmap_protect(pmaps[66], 1196965888, 1196969984, 1);
    }
    ops_count++;
    // Op 504: Extract pmap 52 va=0x61c27000
    if (pmaps[52]) {
        pmap_extract(pmaps[52], 1640132608);
    }
    ops_count++;
    // Op 505: Create pmap 69
    pmaps[69] = pmap_create();
    if (!pmaps[69]) kprint("Warning: pmap_create failed for 69\n");
    ops_count++;
    // Op 506: Remove pmap 59 va=0xb252000
    if (pmaps[59]) {
        pmap_remove(pmaps[59], 186982400);
    }
    ops_count++;
    // Op 507: Protect pmap 66 va=0x47584000
    if (pmaps[66]) {
        pmap_protect(pmaps[66], 1196965888, 1196969984, 1);
    }
    ops_count++;
    // Op 508: Create pmap 70
    pmaps[70] = pmap_create();
    if (!pmaps[70]) kprint("Warning: pmap_create failed for 70\n");
    ops_count++;
    // Op 509: Enter pmap 69 va=0xb807e000 pa=0x7a99c000 prot=0xf
    if (pmaps[69]) {
        pmap_enter(pmaps[69], 3087523840, 2056896512, 15, 0);
    }
    ops_count++;
    // Op 510: Protect pmap 67 va=0xeaf1000
    if (pmaps[67]) {
        pmap_protect(pmaps[67], 246353920, 246358016, 1);
    }
    ops_count++;
    // Op 511: Extract pmap 68 va=0x7740e000
    if (pmaps[68]) {
        pmap_extract(pmaps[68], 2000740352);
    }
    ops_count++;
    // Op 512: Remove pmap 59 va=0xa6dee000
    if (pmaps[59]) {
        pmap_remove(pmaps[59], 2799624192);
    }
    ops_count++;
    // Op 513: Extract pmap 70 va=0x96793000
    if (pmaps[70]) {
        pmap_extract(pmaps[70], 2524524544);
    }
    ops_count++;
    // Op 514: Destroy pmap 52
    if (pmaps[52]) {
        pmap_destroy(pmaps[52]);
        pmaps[52] = 0;
    }
    ops_count++;
    // Op 515: Enter pmap 42 va=0x1fc91000 pa=0x84b25000 prot=0x3
    if (pmaps[42]) {
        pmap_enter(pmaps[42], 533270528, 2226278400, 3, 0);
    }
    ops_count++;
    // Op 516: Enter pmap 56 va=0x5299f000 pa=0xef5af000 prot=0x3
    if (pmaps[56]) {
        pmap_enter(pmaps[56], 1385820160, 4015714304, 3, 0);
    }
    ops_count++;
    // Op 517: Enter pmap 67 va=0x48b77000 pa=0xd89f1000 prot=0x1
    if (pmaps[67]) {
        pmap_enter(pmaps[67], 1219981312, 3634302976, 1, 0);
    }
    ops_count++;
    // Op 518: Enter pmap 69 va=0x8d040000 pa=0x46526000 prot=0x3
    if (pmaps[69]) {
        pmap_enter(pmaps[69], 2365849600, 1179803648, 3, 0);
    }
    ops_count++;
    // Op 519: Remove pmap 68 va=0x88921000
    if (pmaps[68]) {
        pmap_remove(pmaps[68], 2291273728);
    }
    ops_count++;
    // Op 520: Create pmap 71
    pmaps[71] = pmap_create();
    if (!pmaps[71]) kprint("Warning: pmap_create failed for 71\n");
    ops_count++;
    // Op 521: Remove pmap 68 va=0x94a54000
    if (pmaps[68]) {
        pmap_remove(pmaps[68], 2493857792);
    }
    ops_count++;
    // Op 522: Destroy pmap 69
    if (pmaps[69]) {
        pmap_destroy(pmaps[69]);
        pmaps[69] = 0;
    }
    ops_count++;
    // Op 523: Remove pmap 68 va=0x566e4000
    if (pmaps[68]) {
        pmap_remove(pmaps[68], 1450065920);
    }
    ops_count++;
    // Op 524: Create pmap 72
    pmaps[72] = pmap_create();
    if (!pmaps[72]) kprint("Warning: pmap_create failed for 72\n");
    ops_count++;
    // Op 525: Extract pmap 43 va=0x5ebbd000
    if (pmaps[43]) {
        pmap_extract(pmaps[43], 1589366784);
    }
    ops_count++;
    // Op 526: Remove pmap 72 va=0x9277a000
    if (pmaps[72]) {
        pmap_remove(pmaps[72], 2457313280);
    }
    ops_count++;
    // Op 527: Enter pmap 70 va=0x7c0e000 pa=0x7f8f6000 prot=0x5
    if (pmaps[70]) {
        pmap_enter(pmaps[70], 130080768, 2140102656, 5, 0);
    }
    ops_count++;
    // Op 528: Remove pmap 59 va=0x9a3a7000
    if (pmaps[59]) {
        pmap_remove(pmaps[59], 2587521024);
    }
    ops_count++;
    // Op 529: Destroy pmap 72
    if (pmaps[72]) {
        pmap_destroy(pmaps[72]);
        pmaps[72] = 0;
    }
    ops_count++;
    // Op 530: Remove pmap 59 va=0x91370000
    if (pmaps[59]) {
        pmap_remove(pmaps[59], 2436300800);
    }
    ops_count++;
    // Op 531: Create pmap 73
    pmaps[73] = pmap_create();
    if (!pmaps[73]) kprint("Warning: pmap_create failed for 73\n");
    ops_count++;
    // Op 532: Create pmap 74
    pmaps[74] = pmap_create();
    if (!pmaps[74]) kprint("Warning: pmap_create failed for 74\n");
    ops_count++;
    // Op 533: Enter pmap 66 va=0x76e5b000 pa=0x344ad000 prot=0x5
    if (pmaps[66]) {
        pmap_enter(pmaps[66], 1994764288, 877318144, 5, 0);
    }
    ops_count++;
    // Op 534: Remove pmap 60 va=0x5e0a8000
    if (pmaps[60]) {
        pmap_remove(pmaps[60], 1577746432);
    }
    ops_count++;
    // Op 535: Enter pmap 63 va=0x2179f000 pa=0xc2caf000 prot=0x5
    if (pmaps[63]) {
        pmap_enter(pmaps[63], 561639424, 3268079616, 5, 0);
    }
    ops_count++;
    // Op 536: Enter pmap 43 va=0x51b1a000 pa=0x3df28000 prot=0xf
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 1370595328, 1039302656, 15, 0);
    }
    ops_count++;
    // Op 537: Create pmap 75
    pmaps[75] = pmap_create();
    if (!pmaps[75]) kprint("Warning: pmap_create failed for 75\n");
    ops_count++;
    // Op 538: Enter pmap 56 va=0x18c8b000 pa=0xd047000 prot=0x5
    if (pmaps[56]) {
        pmap_enter(pmaps[56], 415805440, 218394624, 5, 0);
    }
    ops_count++;
    // Op 539: Extract pmap 74 va=0x3f8eb000
    if (pmaps[74]) {
        pmap_extract(pmaps[74], 1066315776);
    }
    ops_count++;
    // Op 540: Extract pmap 75 va=0x53d2e000
    if (pmaps[75]) {
        pmap_extract(pmaps[75], 1406328832);
    }
    ops_count++;
    // Op 541: Extract pmap 71 va=0x28cab000
    if (pmaps[71]) {
        pmap_extract(pmaps[71], 684371968);
    }
    ops_count++;
    // Op 542: Enter pmap 67 va=0x77814000 pa=0x7fbeb000 prot=0x5
    if (pmaps[67]) {
        pmap_enter(pmaps[67], 2004959232, 2143203328, 5, 0);
    }
    ops_count++;
    // Op 543: Enter pmap 43 va=0x64ab9000 pa=0x8174c000 prot=0xf
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 1688965120, 2171912192, 15, 0);
    }
    ops_count++;
    // Op 544: Extract pmap 57 va=0x31275000
    if (pmaps[57]) {
        pmap_extract(pmaps[57], 824659968);
    }
    ops_count++;
    // Op 545: Create pmap 76
    pmaps[76] = pmap_create();
    if (!pmaps[76]) kprint("Warning: pmap_create failed for 76\n");
    ops_count++;
    // Op 546: Enter pmap 76 va=0x4926d000 pa=0x89749000 prot=0x1
    if (pmaps[76]) {
        pmap_enter(pmaps[76], 1227280384, 2306117632, 1, 0);
    }
    ops_count++;
    // Op 547: Create pmap 77
    pmaps[77] = pmap_create();
    if (!pmaps[77]) kprint("Warning: pmap_create failed for 77\n");
    ops_count++;
    // Op 548: Enter pmap 71 va=0x673bb000 pa=0x5dbe1000 prot=0x1
    if (pmaps[71]) {
        pmap_enter(pmaps[71], 1731964928, 1572737024, 1, 0);
    }
    ops_count++;
    // Op 549: Enter pmap 63 va=0x5cce6000 pa=0x8db00000 prot=0x5
    if (pmaps[63]) {
        pmap_enter(pmaps[63], 1557028864, 2377121792, 5, 0);
    }
    ops_count++;
    // Op 550: Create pmap 78
    pmaps[78] = pmap_create();
    if (!pmaps[78]) kprint("Warning: pmap_create failed for 78\n");
    ops_count++;
    // Op 551: Enter pmap 78 va=0x479bd000 pa=0xd3579000 prot=0x1
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 1201393664, 3545731072, 1, 0);
    }
    ops_count++;
    // Op 552: Create pmap 79
    pmaps[79] = pmap_create();
    if (!pmaps[79]) kprint("Warning: pmap_create failed for 79\n");
    ops_count++;
    // Op 553: Create pmap 80
    pmaps[80] = pmap_create();
    if (!pmaps[80]) kprint("Warning: pmap_create failed for 80\n");
    ops_count++;
    // Op 554: Enter pmap 70 va=0x8ece2000 pa=0xf06ee000 prot=0x5
    if (pmaps[70]) {
        pmap_enter(pmaps[70], 2395873280, 4033798144, 5, 0);
    }
    ops_count++;
    // Op 555: Protect pmap 63 va=0x1e25a000
    if (pmaps[63]) {
        pmap_protect(pmaps[63], 505782272, 505786368, 15);
    }
    ops_count++;
    // Op 556: Enter pmap 43 va=0x9f65000 pa=0x2324d000 prot=0x5
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 167137280, 589615104, 5, 0);
    }
    ops_count++;
    // Op 557: Enter pmap 80 va=0x83f83000 pa=0x23d8a000 prot=0x5
    if (pmaps[80]) {
        pmap_enter(pmaps[80], 2214080512, 601399296, 5, 0);
    }
    ops_count++;
    // Op 558: Extract pmap 70 va=0x8ece2000
    if (pmaps[70]) {
        pmap_extract(pmaps[70], 2395873280);
    }
    ops_count++;
    // Op 559: Remove pmap 77 va=0x827ca000
    if (pmaps[77]) {
        pmap_remove(pmaps[77], 2189205504);
    }
    ops_count++;
    // Op 560: Enter pmap 79 va=0x4cbf2000 pa=0x79a28000 prot=0x1
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 1287593984, 2040692736, 1, 0);
    }
    ops_count++;
    // Op 561: Enter pmap 57 va=0x6aa27000 pa=0x957d8000 prot=0x5
    if (pmaps[57]) {
        pmap_enter(pmaps[57], 1789030400, 2508029952, 5, 0);
    }
    ops_count++;
    // Op 562: Protect pmap 42 va=0x1fc91000
    if (pmaps[42]) {
        pmap_protect(pmaps[42], 533270528, 533274624, 15);
    }
    ops_count++;
    // Op 563: Enter pmap 79 va=0x93dfe000 pa=0x3a504000 prot=0x1
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 2480922624, 978337792, 1, 0);
    }
    ops_count++;
    // Op 564: Remove pmap 60 va=0x81f39000
    if (pmaps[60]) {
        pmap_remove(pmaps[60], 2180222976);
    }
    ops_count++;
    // Op 565: Create pmap 81
    pmaps[81] = pmap_create();
    if (!pmaps[81]) kprint("Warning: pmap_create failed for 81\n");
    ops_count++;
    // Op 566: Remove pmap 43 va=0x64ab9000
    if (pmaps[43]) {
        pmap_remove(pmaps[43], 1688965120);
    }
    ops_count++;
    // Op 567: Extract pmap 57 va=0x28525000
    if (pmaps[57]) {
        pmap_extract(pmaps[57], 676483072);
    }
    ops_count++;
    // Op 568: Enter pmap 59 va=0x69b7d000 pa=0xb0d4a000 prot=0x3
    if (pmaps[59]) {
        pmap_enter(pmaps[59], 1773654016, 2966724608, 3, 0);
    }
    ops_count++;
    // Op 569: Enter pmap 80 va=0x78bb3000 pa=0xdf80f000 prot=0x1
    if (pmaps[80]) {
        pmap_enter(pmaps[80], 2025533440, 3749769216, 1, 0);
    }
    ops_count++;
    // Op 570: Destroy pmap 70
    if (pmaps[70]) {
        pmap_destroy(pmaps[70]);
        pmaps[70] = 0;
    }
    ops_count++;
    // Op 571: Extract pmap 77 va=0x50522000
    if (pmaps[77]) {
        pmap_extract(pmaps[77], 1347559424);
    }
    ops_count++;
    // Op 572: Extract pmap 76 va=0x57a2c000
    if (pmaps[76]) {
        pmap_extract(pmaps[76], 1470283776);
    }
    ops_count++;
    // Op 573: Enter pmap 67 va=0x9c2e0000 pa=0x7c033000 prot=0x3
    if (pmaps[67]) {
        pmap_enter(pmaps[67], 2620260352, 2080583680, 3, 0);
    }
    ops_count++;
    // Op 574: Extract pmap 67 va=0xbff96000
    if (pmaps[67]) {
        pmap_extract(pmaps[67], 3220791296);
    }
    ops_count++;
    // Op 575: Extract pmap 68 va=0xb4747000
    if (pmaps[68]) {
        pmap_extract(pmaps[68], 3027529728);
    }
    ops_count++;
    // Op 576: Destroy pmap 77
    if (pmaps[77]) {
        pmap_destroy(pmaps[77]);
        pmaps[77] = 0;
    }
    ops_count++;
    // Op 577: Enter pmap 73 va=0x8f7c2000 pa=0xef16f000 prot=0x5
    if (pmaps[73]) {
        pmap_enter(pmaps[73], 2407276544, 4011257856, 5, 0);
    }
    ops_count++;
    // Op 578: Enter pmap 81 va=0xad372000 pa=0x8b1e8000 prot=0xf
    if (pmaps[81]) {
        pmap_enter(pmaps[81], 2906071040, 2334031872, 15, 0);
    }
    ops_count++;
    // Op 579: Extract pmap 74 va=0x257f8000
    if (pmaps[74]) {
        pmap_extract(pmaps[74], 629112832);
    }
    ops_count++;
    // Op 580: Enter pmap 68 va=0xb6d20000 pa=0x144b4000 prot=0x5
    if (pmaps[68]) {
        pmap_enter(pmaps[68], 3067215872, 340475904, 5, 0);
    }
    ops_count++;
    // Op 581: Extract pmap 67 va=0xbff96000
    if (pmaps[67]) {
        pmap_extract(pmaps[67], 3220791296);
    }
    ops_count++;
    // Op 582: Destroy pmap 80
    if (pmaps[80]) {
        pmap_destroy(pmaps[80]);
        pmaps[80] = 0;
    }
    ops_count++;
    // Op 583: Enter pmap 81 va=0xb2385000 pa=0x4598f000 prot=0x3
    if (pmaps[81]) {
        pmap_enter(pmaps[81], 2990034944, 1167650816, 3, 0);
    }
    ops_count++;
    // Op 584: Create pmap 82
    pmaps[82] = pmap_create();
    if (!pmaps[82]) kprint("Warning: pmap_create failed for 82\n");
    ops_count++;
    // Op 585: Protect pmap 66 va=0x47584000
    if (pmaps[66]) {
        pmap_protect(pmaps[66], 1196965888, 1196969984, 1);
    }
    ops_count++;
    // Op 586: Remove pmap 79 va=0x4cbf2000
    if (pmaps[79]) {
        pmap_remove(pmaps[79], 1287593984);
    }
    ops_count++;
    // Op 587: Remove pmap 43 va=0x2339c000
    if (pmaps[43]) {
        pmap_remove(pmaps[43], 590987264);
    }
    ops_count++;
    // Op 588: Enter pmap 78 va=0x19bd5000 pa=0xae729000 prot=0x3
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 431837184, 2926743552, 3, 0);
    }
    ops_count++;
    // Op 589: Create pmap 83
    pmaps[83] = pmap_create();
    if (!pmaps[83]) kprint("Warning: pmap_create failed for 83\n");
    ops_count++;
    // Op 590: Remove pmap 60 va=0x682b2000
    if (pmaps[60]) {
        pmap_remove(pmaps[60], 1747656704);
    }
    ops_count++;
    // Op 591: Remove pmap 78 va=0x19bd5000
    if (pmaps[78]) {
        pmap_remove(pmaps[78], 431837184);
    }
    ops_count++;
    // Op 592: Remove pmap 68 va=0xb6d20000
    if (pmaps[68]) {
        pmap_remove(pmaps[68], 3067215872);
    }
    ops_count++;
    // Op 593: Enter pmap 43 va=0x16e6f000 pa=0xa70d0000 prot=0x3
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 384233472, 2802647040, 3, 0);
    }
    ops_count++;
    // Op 594: Enter pmap 43 va=0x2cd11000 pa=0x6b0a8000 prot=0x3
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 751898624, 1795850240, 3, 0);
    }
    ops_count++;
    // Op 595: Extract pmap 43 va=0x65a17000
    if (pmaps[43]) {
        pmap_extract(pmaps[43], 1705078784);
    }
    ops_count++;
    // Op 596: Extract pmap 68 va=0x25f6000
    if (pmaps[68]) {
        pmap_extract(pmaps[68], 39804928);
    }
    ops_count++;
    // Op 597: Enter pmap 83 va=0x1b781000 pa=0xedbf3000 prot=0x5
    if (pmaps[83]) {
        pmap_enter(pmaps[83], 460853248, 3988729856, 5, 0);
    }
    ops_count++;
    // Op 598: Enter pmap 81 va=0x8639c000 pa=0x7e810000 prot=0x3
    if (pmaps[81]) {
        pmap_enter(pmaps[81], 2251931648, 2122383360, 3, 0);
    }
    ops_count++;
    // Op 599: Extract pmap 76 va=0x4926d000
    if (pmaps[76]) {
        pmap_extract(pmaps[76], 1227280384);
    }
    ops_count++;
    kprint(".");
    // Op 600: Enter pmap 76 va=0xa5f61000 pa=0x41ec1000 prot=0x3
    if (pmaps[76]) {
        pmap_enter(pmaps[76], 2784366592, 1105989632, 3, 0);
    }
    ops_count++;
    // Op 601: Create pmap 84
    pmaps[84] = pmap_create();
    if (!pmaps[84]) kprint("Warning: pmap_create failed for 84\n");
    ops_count++;
    // Op 602: Enter pmap 68 va=0x915a2000 pa=0xacb58000 prot=0x3
    if (pmaps[68]) {
        pmap_enter(pmaps[68], 2438602752, 2897575936, 3, 0);
    }
    ops_count++;
    // Op 603: Destroy pmap 84
    if (pmaps[84]) {
        pmap_destroy(pmaps[84]);
        pmaps[84] = 0;
    }
    ops_count++;
    // Op 604: Extract pmap 75 va=0x16348000
    if (pmaps[75]) {
        pmap_extract(pmaps[75], 372539392);
    }
    ops_count++;
    // Op 605: Enter pmap 57 va=0x2f4de000 pa=0xfb5fb000 prot=0x3
    if (pmaps[57]) {
        pmap_enter(pmaps[57], 793632768, 4217352192, 3, 0);
    }
    ops_count++;
    // Op 606: Enter pmap 66 va=0x1bfa000 pa=0x42d2d000 prot=0xf
    if (pmaps[66]) {
        pmap_enter(pmaps[66], 29335552, 1121112064, 15, 0);
    }
    ops_count++;
    // Op 607: Destroy pmap 67
    if (pmaps[67]) {
        pmap_destroy(pmaps[67]);
        pmaps[67] = 0;
    }
    ops_count++;
    // Op 608: Extract pmap 71 va=0x673bb000
    if (pmaps[71]) {
        pmap_extract(pmaps[71], 1731964928);
    }
    ops_count++;
    // Op 609: Enter pmap 74 va=0xb1595000 pa=0x3c8e5000 prot=0x1
    if (pmaps[74]) {
        pmap_enter(pmaps[74], 2975420416, 1015959552, 1, 0);
    }
    ops_count++;
    // Op 610: Remove pmap 78 va=0x479bd000
    if (pmaps[78]) {
        pmap_remove(pmaps[78], 1201393664);
    }
    ops_count++;
    // Op 611: Destroy pmap 81
    if (pmaps[81]) {
        pmap_destroy(pmaps[81]);
        pmaps[81] = 0;
    }
    ops_count++;
    // Op 612: Extract pmap 71 va=0x673bb000
    if (pmaps[71]) {
        pmap_extract(pmaps[71], 1731964928);
    }
    ops_count++;
    // Op 613: Enter pmap 66 va=0x3818d000 pa=0xf5259000 prot=0x3
    if (pmaps[66]) {
        pmap_enter(pmaps[66], 941150208, 4112879616, 3, 0);
    }
    ops_count++;
    // Op 614: Enter pmap 78 va=0xbf6b9000 pa=0xfd347000 prot=0x5
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 3211497472, 4248072192, 5, 0);
    }
    ops_count++;
    // Op 615: Enter pmap 83 va=0x78884000 pa=0xc1f1b000 prot=0x3
    if (pmaps[83]) {
        pmap_enter(pmaps[83], 2022195200, 3253841920, 3, 0);
    }
    ops_count++;
    // Op 616: Protect pmap 56 va=0x18c8b000
    if (pmaps[56]) {
        pmap_protect(pmaps[56], 415805440, 415809536, 15);
    }
    ops_count++;
    // Op 617: Enter pmap 74 va=0x13fe9000 pa=0xeae08000 prot=0x1
    if (pmaps[74]) {
        pmap_enter(pmaps[74], 335450112, 3940581376, 1, 0);
    }
    ops_count++;
    // Op 618: Create pmap 85
    pmaps[85] = pmap_create();
    if (!pmaps[85]) kprint("Warning: pmap_create failed for 85\n");
    ops_count++;
    // Op 619: Enter pmap 82 va=0x33c09000 pa=0x92ac9000 prot=0x3
    if (pmaps[82]) {
        pmap_enter(pmaps[82], 868257792, 2460782592, 3, 0);
    }
    ops_count++;
    // Op 620: Destroy pmap 82
    if (pmaps[82]) {
        pmap_destroy(pmaps[82]);
        pmaps[82] = 0;
    }
    ops_count++;
    // Op 621: Enter pmap 63 va=0xb787f000 pa=0x95592000 prot=0xf
    if (pmaps[63]) {
        pmap_enter(pmaps[63], 3079139328, 2505646080, 15, 0);
    }
    ops_count++;
    // Op 622: Create pmap 86
    pmaps[86] = pmap_create();
    if (!pmaps[86]) kprint("Warning: pmap_create failed for 86\n");
    ops_count++;
    // Op 623: Enter pmap 43 va=0x74143000 pa=0x21d71000 prot=0xf
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 1947480064, 567742464, 15, 0);
    }
    ops_count++;
    // Op 624: Enter pmap 43 va=0x8f0d6000 pa=0x766e6000 prot=0x5
    if (pmaps[43]) {
        pmap_enter(pmaps[43], 2400018432, 1986945024, 5, 0);
    }
    ops_count++;
    // Op 625: Enter pmap 42 va=0xbeed1000 pa=0x37d86000 prot=0x1
    if (pmaps[42]) {
        pmap_enter(pmaps[42], 3203207168, 936927232, 1, 0);
    }
    ops_count++;
    // Op 626: Create pmap 87
    pmaps[87] = pmap_create();
    if (!pmaps[87]) kprint("Warning: pmap_create failed for 87\n");
    ops_count++;
    // Op 627: Enter pmap 56 va=0x8a89c000 pa=0xfe8ee000 prot=0x1
    if (pmaps[56]) {
        pmap_enter(pmaps[56], 2324283392, 4270776320, 1, 0);
    }
    ops_count++;
    // Op 628: Extract pmap 56 va=0x78d9b000
    if (pmaps[56]) {
        pmap_extract(pmaps[56], 2027532288);
    }
    ops_count++;
    // Op 629: Create pmap 88
    pmaps[88] = pmap_create();
    if (!pmaps[88]) kprint("Warning: pmap_create failed for 88\n");
    ops_count++;
    // Op 630: Enter pmap 59 va=0xa43b0000 pa=0xfa840000 prot=0xf
    if (pmaps[59]) {
        pmap_enter(pmaps[59], 2755330048, 4202954752, 15, 0);
    }
    ops_count++;
    // Op 631: Enter pmap 75 va=0x72d36000 pa=0xdeaf6000 prot=0xf
    if (pmaps[75]) {
        pmap_enter(pmaps[75], 1926455296, 3736035328, 15, 0);
    }
    ops_count++;
    // Op 632: Enter pmap 85 va=0x220ba000 pa=0xa75e0000 prot=0x5
    if (pmaps[85]) {
        pmap_enter(pmaps[85], 571187200, 2807955456, 5, 0);
    }
    ops_count++;
    // Op 633: Create pmap 89
    pmaps[89] = pmap_create();
    if (!pmaps[89]) kprint("Warning: pmap_create failed for 89\n");
    ops_count++;
    // Op 634: Extract pmap 75 va=0x72d36000
    if (pmaps[75]) {
        pmap_extract(pmaps[75], 1926455296);
    }
    ops_count++;
    // Op 635: Protect pmap 42 va=0xbeed1000
    if (pmaps[42]) {
        pmap_protect(pmaps[42], 3203207168, 3203211264, 15);
    }
    ops_count++;
    // Op 636: Remove pmap 85 va=0x220ba000
    if (pmaps[85]) {
        pmap_remove(pmaps[85], 571187200);
    }
    ops_count++;
    // Op 637: Enter pmap 66 va=0x3f5d0000 pa=0x75fb8000 prot=0x5
    if (pmaps[66]) {
        pmap_enter(pmaps[66], 1063059456, 1979416576, 5, 0);
    }
    ops_count++;
    // Op 638: Destroy pmap 63
    if (pmaps[63]) {
        pmap_destroy(pmaps[63]);
        pmaps[63] = 0;
    }
    ops_count++;
    // Op 639: Extract pmap 57 va=0x2f4de000
    if (pmaps[57]) {
        pmap_extract(pmaps[57], 793632768);
    }
    ops_count++;
    // Op 640: Remove pmap 42 va=0x95a5c000
    if (pmaps[42]) {
        pmap_remove(pmaps[42], 2510667776);
    }
    ops_count++;
    // Op 641: Destroy pmap 57
    if (pmaps[57]) {
        pmap_destroy(pmaps[57]);
        pmaps[57] = 0;
    }
    ops_count++;
    // Op 642: Remove pmap 83 va=0x1b781000
    if (pmaps[83]) {
        pmap_remove(pmaps[83], 460853248);
    }
    ops_count++;
    // Op 643: Destroy pmap 43
    if (pmaps[43]) {
        pmap_destroy(pmaps[43]);
        pmaps[43] = 0;
    }
    ops_count++;
    // Op 644: Enter pmap 73 va=0x8a2dc000 pa=0x37aef000 prot=0x1
    if (pmaps[73]) {
        pmap_enter(pmaps[73], 2318254080, 934211584, 1, 0);
    }
    ops_count++;
    // Op 645: Create pmap 90
    pmaps[90] = pmap_create();
    if (!pmaps[90]) kprint("Warning: pmap_create failed for 90\n");
    ops_count++;
    // Op 646: Extract pmap 73 va=0xbb44c000
    if (pmaps[73]) {
        pmap_extract(pmaps[73], 3141844992);
    }
    ops_count++;
    // Op 647: Remove pmap 89 va=0x99113000
    if (pmaps[89]) {
        pmap_remove(pmaps[89], 2568040448);
    }
    ops_count++;
    // Op 648: Protect pmap 76 va=0x4926d000
    if (pmaps[76]) {
        pmap_protect(pmaps[76], 1227280384, 1227284480, 15);
    }
    ops_count++;
    // Op 649: Extract pmap 87 va=0x4cedb000
    if (pmaps[87]) {
        pmap_extract(pmaps[87], 1290645504);
    }
    ops_count++;
    // Op 650: Enter pmap 88 va=0x97778000 pa=0xe144e000 prot=0x3
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 2541191168, 3779387392, 3, 0);
    }
    ops_count++;
    // Op 651: Remove pmap 83 va=0x78884000
    if (pmaps[83]) {
        pmap_remove(pmaps[83], 2022195200);
    }
    ops_count++;
    // Op 652: Create pmap 91
    pmaps[91] = pmap_create();
    if (!pmaps[91]) kprint("Warning: pmap_create failed for 91\n");
    ops_count++;
    // Op 653: Enter pmap 91 va=0xabdd2000 pa=0x61a35000 prot=0x5
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 2883395584, 1638092800, 5, 0);
    }
    ops_count++;
    // Op 654: Remove pmap 83 va=0x26333000
    if (pmaps[83]) {
        pmap_remove(pmaps[83], 640888832);
    }
    ops_count++;
    // Op 655: Enter pmap 68 va=0x89b5b000 pa=0x79488000 prot=0x3
    if (pmaps[68]) {
        pmap_enter(pmaps[68], 2310385664, 2034794496, 3, 0);
    }
    ops_count++;
    // Op 656: Protect pmap 75 va=0x72d36000
    if (pmaps[75]) {
        pmap_protect(pmaps[75], 1926455296, 1926459392, 15);
    }
    ops_count++;
    // Op 657: Extract pmap 88 va=0x97778000
    if (pmaps[88]) {
        pmap_extract(pmaps[88], 2541191168);
    }
    ops_count++;
    // Op 658: Remove pmap 66 va=0x3f5d0000
    if (pmaps[66]) {
        pmap_remove(pmaps[66], 1063059456);
    }
    ops_count++;
    // Op 659: Destroy pmap 71
    if (pmaps[71]) {
        pmap_destroy(pmaps[71]);
        pmaps[71] = 0;
    }
    ops_count++;
    // Op 660: Enter pmap 59 va=0x73254000 pa=0xd8edb000 prot=0x5
    if (pmaps[59]) {
        pmap_enter(pmaps[59], 1931821056, 3639455744, 5, 0);
    }
    ops_count++;
    // Op 661: Create pmap 92
    pmaps[92] = pmap_create();
    if (!pmaps[92]) kprint("Warning: pmap_create failed for 92\n");
    ops_count++;
    // Op 662: Protect pmap 73 va=0x8f7c2000
    if (pmaps[73]) {
        pmap_protect(pmaps[73], 2407276544, 2407280640, 15);
    }
    ops_count++;
    // Op 663: Enter pmap 91 va=0x9a592000 pa=0xa2fc000 prot=0x1
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 2589532160, 170901504, 1, 0);
    }
    ops_count++;
    // Op 664: Destroy pmap 90
    if (pmaps[90]) {
        pmap_destroy(pmaps[90]);
        pmaps[90] = 0;
    }
    ops_count++;
    // Op 665: Destroy pmap 73
    if (pmaps[73]) {
        pmap_destroy(pmaps[73]);
        pmaps[73] = 0;
    }
    ops_count++;
    // Op 666: Extract pmap 79 va=0x93dfe000
    if (pmaps[79]) {
        pmap_extract(pmaps[79], 2480922624);
    }
    ops_count++;
    // Op 667: Destroy pmap 74
    if (pmaps[74]) {
        pmap_destroy(pmaps[74]);
        pmaps[74] = 0;
    }
    ops_count++;
    // Op 668: Extract pmap 60 va=0x7a9a9000
    if (pmaps[60]) {
        pmap_extract(pmaps[60], 2056949760);
    }
    ops_count++;
    // Op 669: Extract pmap 75 va=0x72d36000
    if (pmaps[75]) {
        pmap_extract(pmaps[75], 1926455296);
    }
    ops_count++;
    // Op 670: Remove pmap 79 va=0x93dfe000
    if (pmaps[79]) {
        pmap_remove(pmaps[79], 2480922624);
    }
    ops_count++;
    // Op 671: Enter pmap 85 va=0x4ef4d000 pa=0x4305b000 prot=0x5
    if (pmaps[85]) {
        pmap_enter(pmaps[85], 1324666880, 1124446208, 5, 0);
    }
    ops_count++;
    // Op 672: Enter pmap 89 va=0x774c1000 pa=0x194e6000 prot=0xf
    if (pmaps[89]) {
        pmap_enter(pmaps[89], 2001473536, 424566784, 15, 0);
    }
    ops_count++;
    // Op 673: Extract pmap 85 va=0x4ef4d000
    if (pmaps[85]) {
        pmap_extract(pmaps[85], 1324666880);
    }
    ops_count++;
    // Op 674: Remove pmap 76 va=0x4926d000
    if (pmaps[76]) {
        pmap_remove(pmaps[76], 1227280384);
    }
    ops_count++;
    // Op 675: Create pmap 93
    pmaps[93] = pmap_create();
    if (!pmaps[93]) kprint("Warning: pmap_create failed for 93\n");
    ops_count++;
    // Op 676: Remove pmap 93 va=0x9442c000
    if (pmaps[93]) {
        pmap_remove(pmaps[93], 2487402496);
    }
    ops_count++;
    // Op 677: Destroy pmap 83
    if (pmaps[83]) {
        pmap_destroy(pmaps[83]);
        pmaps[83] = 0;
    }
    ops_count++;
    // Op 678: Destroy pmap 76
    if (pmaps[76]) {
        pmap_destroy(pmaps[76]);
        pmaps[76] = 0;
    }
    ops_count++;
    // Op 679: Enter pmap 86 va=0x41e03000 pa=0xc37b9000 prot=0xf
    if (pmaps[86]) {
        pmap_enter(pmaps[86], 1105211392, 3279654912, 15, 0);
    }
    ops_count++;
    // Op 680: Extract pmap 92 va=0xbb970000
    if (pmaps[92]) {
        pmap_extract(pmaps[92], 3147235328);
    }
    ops_count++;
    // Op 681: Protect pmap 68 va=0x915a2000
    if (pmaps[68]) {
        pmap_protect(pmaps[68], 2438602752, 2438606848, 15);
    }
    ops_count++;
    // Op 682: Destroy pmap 56
    if (pmaps[56]) {
        pmap_destroy(pmaps[56]);
        pmaps[56] = 0;
    }
    ops_count++;
    // Op 683: Enter pmap 60 va=0xab103000 pa=0xc7b7000 prot=0x1
    if (pmaps[60]) {
        pmap_enter(pmaps[60], 2869964800, 209416192, 1, 0);
    }
    ops_count++;
    // Op 684: Enter pmap 79 va=0x11666000 pa=0xb5191000 prot=0x3
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 291921920, 3038318592, 3, 0);
    }
    ops_count++;
    // Op 685: Create pmap 94
    pmaps[94] = pmap_create();
    if (!pmaps[94]) kprint("Warning: pmap_create failed for 94\n");
    ops_count++;
    // Op 686: Enter pmap 88 va=0xf79b000 pa=0x9e3a7000 prot=0xf
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 259633152, 2654629888, 15, 0);
    }
    ops_count++;
    // Op 687: Remove pmap 42 va=0x2a1fa000
    if (pmaps[42]) {
        pmap_remove(pmaps[42], 706715648);
    }
    ops_count++;
    // Op 688: Enter pmap 91 va=0x308b000 pa=0x4582000 prot=0x5
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 50900992, 72884224, 5, 0);
    }
    ops_count++;
    // Op 689: Enter pmap 86 va=0x4638000 pa=0x80a13000 prot=0xf
    if (pmaps[86]) {
        pmap_enter(pmaps[86], 73629696, 2158047232, 15, 0);
    }
    ops_count++;
    // Op 690: Protect pmap 75 va=0x72d36000
    if (pmaps[75]) {
        pmap_protect(pmaps[75], 1926455296, 1926459392, 1);
    }
    ops_count++;
    // Op 691: Enter pmap 79 va=0x31228000 pa=0x9eb9f000 prot=0x5
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 824344576, 2662985728, 5, 0);
    }
    ops_count++;
    // Op 692: Protect pmap 85 va=0x4ef4d000
    if (pmaps[85]) {
        pmap_protect(pmaps[85], 1324666880, 1324670976, 1);
    }
    ops_count++;
    // Op 693: Enter pmap 89 va=0x7580f000 pa=0x90b3f000 prot=0x3
    if (pmaps[89]) {
        pmap_enter(pmaps[89], 1971384320, 2427711488, 3, 0);
    }
    ops_count++;
    // Op 694: Extract pmap 79 va=0x31228000
    if (pmaps[79]) {
        pmap_extract(pmaps[79], 824344576);
    }
    ops_count++;
    // Op 695: Remove pmap 59 va=0xa43b0000
    if (pmaps[59]) {
        pmap_remove(pmaps[59], 2755330048);
    }
    ops_count++;
    // Op 696: Enter pmap 89 va=0x8d7de000 pa=0x79eb7000 prot=0x1
    if (pmaps[89]) {
        pmap_enter(pmaps[89], 2373836800, 2045472768, 1, 0);
    }
    ops_count++;
    // Op 697: Remove pmap 75 va=0x72d36000
    if (pmaps[75]) {
        pmap_remove(pmaps[75], 1926455296);
    }
    ops_count++;
    // Op 698: Enter pmap 91 va=0xa518b000 pa=0xc8dce000 prot=0x5
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 2769858560, 3369918464, 5, 0);
    }
    ops_count++;
    // Op 699: Remove pmap 66 va=0x47584000
    if (pmaps[66]) {
        pmap_remove(pmaps[66], 1196965888);
    }
    ops_count++;
    kprint(".");
    // Op 700: Destroy pmap 93
    if (pmaps[93]) {
        pmap_destroy(pmaps[93]);
        pmaps[93] = 0;
    }
    ops_count++;
    // Op 701: Enter pmap 60 va=0xaf36c000 pa=0xed032000 prot=0x5
    if (pmaps[60]) {
        pmap_enter(pmaps[60], 2939600896, 3976404992, 5, 0);
    }
    ops_count++;
    // Op 702: Extract pmap 59 va=0x69b7d000
    if (pmaps[59]) {
        pmap_extract(pmaps[59], 1773654016);
    }
    ops_count++;
    // Op 703: Create pmap 95
    pmaps[95] = pmap_create();
    if (!pmaps[95]) kprint("Warning: pmap_create failed for 95\n");
    ops_count++;
    // Op 704: Destroy pmap 68
    if (pmaps[68]) {
        pmap_destroy(pmaps[68]);
        pmaps[68] = 0;
    }
    ops_count++;
    // Op 705: Protect pmap 91 va=0xabdd2000
    if (pmaps[91]) {
        pmap_protect(pmaps[91], 2883395584, 2883399680, 15);
    }
    ops_count++;
    // Op 706: Destroy pmap 85
    if (pmaps[85]) {
        pmap_destroy(pmaps[85]);
        pmaps[85] = 0;
    }
    ops_count++;
    // Op 707: Remove pmap 94 va=0x639fd000
    if (pmaps[94]) {
        pmap_remove(pmaps[94], 1671417856);
    }
    ops_count++;
    // Op 708: Remove pmap 87 va=0x54ec4000
    if (pmaps[87]) {
        pmap_remove(pmaps[87], 1424769024);
    }
    ops_count++;
    // Op 709: Enter pmap 91 va=0x23d3d000 pa=0xca6e1000 prot=0x1
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 601083904, 3396210688, 1, 0);
    }
    ops_count++;
    // Op 710: Enter pmap 78 va=0xdbf1000 pa=0x1911e000 prot=0xf
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 230625280, 420601856, 15, 0);
    }
    ops_count++;
    // Op 711: Destroy pmap 59
    if (pmaps[59]) {
        pmap_destroy(pmaps[59]);
        pmaps[59] = 0;
    }
    ops_count++;
    // Op 712: Enter pmap 94 va=0x9d265000 pa=0xf4819000 prot=0xf
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 2636533760, 4102131712, 15, 0);
    }
    ops_count++;
    // Op 713: Extract pmap 79 va=0x11666000
    if (pmaps[79]) {
        pmap_extract(pmaps[79], 291921920);
    }
    ops_count++;
    // Op 714: Enter pmap 79 va=0x8d93f000 pa=0xf0feb000 prot=0xf
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 2375282688, 4043223040, 15, 0);
    }
    ops_count++;
    // Op 715: Enter pmap 91 va=0x95fbf000 pa=0xa931c000 prot=0xf
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 2516316160, 2838609920, 15, 0);
    }
    ops_count++;
    // Op 716: Create pmap 96
    pmaps[96] = pmap_create();
    if (!pmaps[96]) kprint("Warning: pmap_create failed for 96\n");
    ops_count++;
    // Op 717: Remove pmap 94 va=0x9d265000
    if (pmaps[94]) {
        pmap_remove(pmaps[94], 2636533760);
    }
    ops_count++;
    // Op 718: Protect pmap 60 va=0xab103000
    if (pmaps[60]) {
        pmap_protect(pmaps[60], 2869964800, 2869968896, 1);
    }
    ops_count++;
    // Op 719: Enter pmap 94 va=0xa2476000 pa=0x278eb000 prot=0xf
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 2722586624, 663662592, 15, 0);
    }
    ops_count++;
    // Op 720: Enter pmap 60 va=0x176df000 pa=0x1647000 prot=0x5
    if (pmaps[60]) {
        pmap_enter(pmaps[60], 393080832, 23359488, 5, 0);
    }
    ops_count++;
    // Op 721: Enter pmap 94 va=0x450f7000 pa=0x1a3bd000 prot=0x3
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 1158639616, 440127488, 3, 0);
    }
    ops_count++;
    // Op 722: Create pmap 97
    pmaps[97] = pmap_create();
    if (!pmaps[97]) kprint("Warning: pmap_create failed for 97\n");
    ops_count++;
    // Op 723: Enter pmap 95 va=0x1a717000 pa=0x6c91000 prot=0x1
    if (pmaps[95]) {
        pmap_enter(pmaps[95], 443641856, 113840128, 1, 0);
    }
    ops_count++;
    // Op 724: Enter pmap 66 va=0x98d01000 pa=0x99296000 prot=0x5
    if (pmaps[66]) {
        pmap_enter(pmaps[66], 2563772416, 2569625600, 5, 0);
    }
    ops_count++;
    // Op 725: Extract pmap 42 va=0x1fc91000
    if (pmaps[42]) {
        pmap_extract(pmaps[42], 533270528);
    }
    ops_count++;
    // Op 726: Remove pmap 79 va=0x8d93f000
    if (pmaps[79]) {
        pmap_remove(pmaps[79], 2375282688);
    }
    ops_count++;
    // Op 727: Enter pmap 78 va=0x44d7e000 pa=0x4d26f000 prot=0x5
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 1154998272, 1294397440, 5, 0);
    }
    ops_count++;
    // Op 728: Enter pmap 66 va=0x2ace2000 pa=0x6f56f000 prot=0x5
    if (pmaps[66]) {
        pmap_enter(pmaps[66], 718151680, 1867968512, 5, 0);
    }
    ops_count++;
    // Op 729: Enter pmap 97 va=0x13b12000 pa=0x5c82a000 prot=0x5
    if (pmaps[97]) {
        pmap_enter(pmaps[97], 330375168, 1552064512, 5, 0);
    }
    ops_count++;
    // Op 730: Extract pmap 97 va=0x13b12000
    if (pmaps[97]) {
        pmap_extract(pmaps[97], 330375168);
    }
    ops_count++;
    // Op 731: Enter pmap 78 va=0x4dfe2000 pa=0x1cb8000 prot=0xf
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 1308499968, 30113792, 15, 0);
    }
    ops_count++;
    // Op 732: Enter pmap 94 va=0x545ce000 pa=0x70c27000 prot=0x5
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 1415372800, 1891790848, 5, 0);
    }
    ops_count++;
    // Op 733: Enter pmap 78 va=0x4ce96000 pa=0x52271000 prot=0x3
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 1290362880, 1378291712, 3, 0);
    }
    ops_count++;
    // Op 734: Enter pmap 79 va=0x65f31000 pa=0xf80e6000 prot=0x5
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 1710428160, 4161691648, 5, 0);
    }
    ops_count++;
    // Op 735: Enter pmap 97 va=0x936ce000 pa=0xc83f7000 prot=0x3
    if (pmaps[97]) {
        pmap_enter(pmaps[97], 2473385984, 3359600640, 3, 0);
    }
    ops_count++;
    // Op 736: Enter pmap 88 va=0x64977000 pa=0x5d81a000 prot=0x1
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 1687646208, 1568776192, 1, 0);
    }
    ops_count++;
    // Op 737: Extract pmap 86 va=0x41e03000
    if (pmaps[86]) {
        pmap_extract(pmaps[86], 1105211392);
    }
    ops_count++;
    // Op 738: Remove pmap 42 va=0x1fc91000
    if (pmaps[42]) {
        pmap_remove(pmaps[42], 533270528);
    }
    ops_count++;
    // Op 739: Extract pmap 86 va=0x4638000
    if (pmaps[86]) {
        pmap_extract(pmaps[86], 73629696);
    }
    ops_count++;
    // Op 740: Protect pmap 66 va=0x98d01000
    if (pmaps[66]) {
        pmap_protect(pmaps[66], 2563772416, 2563776512, 15);
    }
    ops_count++;
    // Op 741: Create pmap 98
    pmaps[98] = pmap_create();
    if (!pmaps[98]) kprint("Warning: pmap_create failed for 98\n");
    ops_count++;
    // Op 742: Enter pmap 88 va=0xa89e3000 pa=0x2785e000 prot=0xf
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 2828939264, 663085056, 15, 0);
    }
    ops_count++;
    // Op 743: Protect pmap 66 va=0x98d01000
    if (pmaps[66]) {
        pmap_protect(pmaps[66], 2563772416, 2563776512, 15);
    }
    ops_count++;
    // Op 744: Remove pmap 98 va=0xb113e000
    if (pmaps[98]) {
        pmap_remove(pmaps[98], 2970869760);
    }
    ops_count++;
    // Op 745: Enter pmap 60 va=0x5c0a2000 pa=0xb3ae4000 prot=0x1
    if (pmaps[60]) {
        pmap_enter(pmaps[60], 1544167424, 3014541312, 1, 0);
    }
    ops_count++;
    // Op 746: Create pmap 99
    pmaps[99] = pmap_create();
    if (!pmaps[99]) kprint("Warning: pmap_create failed for 99\n");
    ops_count++;
    // Op 747: Destroy pmap 92
    if (pmaps[92]) {
        pmap_destroy(pmaps[92]);
        pmaps[92] = 0;
    }
    ops_count++;
    // Op 748: Destroy pmap 60
    if (pmaps[60]) {
        pmap_destroy(pmaps[60]);
        pmaps[60] = 0;
    }
    ops_count++;
    // Op 749: Remove pmap 96 va=0x54e39000
    if (pmaps[96]) {
        pmap_remove(pmaps[96], 1424199680);
    }
    ops_count++;
    // Op 750: Extract pmap 97 va=0x13b12000
    if (pmaps[97]) {
        pmap_extract(pmaps[97], 330375168);
    }
    ops_count++;
    // Op 751: Enter pmap 88 va=0x721e9000 pa=0x5fd64000 prot=0xf
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 1914605568, 1607876608, 15, 0);
    }
    ops_count++;
    // Op 752: Enter pmap 79 va=0x584fd000 pa=0xff797000 prot=0x1
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 1481625600, 4286148608, 1, 0);
    }
    ops_count++;
    // Op 753: Enter pmap 91 va=0x6a7d6000 pa=0xebb90000 prot=0x1
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 1786601472, 3954769920, 1, 0);
    }
    ops_count++;
    // Op 754: Create pmap 100
    pmaps[100] = pmap_create();
    if (!pmaps[100]) kprint("Warning: pmap_create failed for 100\n");
    ops_count++;
    // Op 755: Extract pmap 79 va=0x65f31000
    if (pmaps[79]) {
        pmap_extract(pmaps[79], 1710428160);
    }
    ops_count++;
    // Op 756: Enter pmap 87 va=0x8567d000 pa=0x7bb10000 prot=0x5
    if (pmaps[87]) {
        pmap_enter(pmaps[87], 2238173184, 2075197440, 5, 0);
    }
    ops_count++;
    // Op 757: Enter pmap 98 va=0xb8d4a000 pa=0xb26f1000 prot=0xf
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 3100942336, 2993623040, 15, 0);
    }
    ops_count++;
    // Op 758: Enter pmap 91 va=0xb706000 pa=0xcf785000 prot=0x1
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 191913984, 3480768512, 1, 0);
    }
    ops_count++;
    // Op 759: Create pmap 101
    pmaps[101] = pmap_create();
    if (!pmaps[101]) kprint("Warning: pmap_create failed for 101\n");
    ops_count++;
    // Op 760: Enter pmap 78 va=0xad591000 pa=0xcce01000 prot=0x3
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 2908295168, 3437236224, 3, 0);
    }
    ops_count++;
    // Op 761: Extract pmap 88 va=0xa89e3000
    if (pmaps[88]) {
        pmap_extract(pmaps[88], 2828939264);
    }
    ops_count++;
    // Op 762: Destroy pmap 101
    if (pmaps[101]) {
        pmap_destroy(pmaps[101]);
        pmaps[101] = 0;
    }
    ops_count++;
    // Op 763: Enter pmap 98 va=0x3b5af000 pa=0xcc49b000 prot=0xf
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 995815424, 3427381248, 15, 0);
    }
    ops_count++;
    // Op 764: Remove pmap 86 va=0x4638000
    if (pmaps[86]) {
        pmap_remove(pmaps[86], 73629696);
    }
    ops_count++;
    // Op 765: Enter pmap 87 va=0x73176000 pa=0x97aaa000 prot=0xf
    if (pmaps[87]) {
        pmap_enter(pmaps[87], 1930911744, 2544541696, 15, 0);
    }
    ops_count++;
    // Op 766: Enter pmap 87 va=0x34ae5000 pa=0x47519000 prot=0x1
    if (pmaps[87]) {
        pmap_enter(pmaps[87], 883838976, 1196527616, 1, 0);
    }
    ops_count++;
    // Op 767: Remove pmap 86 va=0x41e03000
    if (pmaps[86]) {
        pmap_remove(pmaps[86], 1105211392);
    }
    ops_count++;
    // Op 768: Enter pmap 98 va=0x1d2c3000 pa=0x43399000 prot=0xf
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 489435136, 1127845888, 15, 0);
    }
    ops_count++;
    // Op 769: Enter pmap 94 va=0x98d32000 pa=0x638dd000 prot=0xf
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 2563973120, 1670238208, 15, 0);
    }
    ops_count++;
    // Op 770: Remove pmap 95 va=0x1a717000
    if (pmaps[95]) {
        pmap_remove(pmaps[95], 443641856);
    }
    ops_count++;
    // Op 771: Protect pmap 91 va=0x9a592000
    if (pmaps[91]) {
        pmap_protect(pmaps[91], 2589532160, 2589536256, 1);
    }
    ops_count++;
    // Op 772: Enter pmap 88 va=0x4e2d7000 pa=0x1e005000 prot=0x3
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 1311600640, 503336960, 3, 0);
    }
    ops_count++;
    // Op 773: Enter pmap 79 va=0x82c57000 pa=0x63825000 prot=0xf
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 2193977344, 1669484544, 15, 0);
    }
    ops_count++;
    // Op 774: Remove pmap 96 va=0x6c4c5000
    if (pmaps[96]) {
        pmap_remove(pmaps[96], 1816940544);
    }
    ops_count++;
    // Op 775: Destroy pmap 86
    if (pmaps[86]) {
        pmap_destroy(pmaps[86]);
        pmaps[86] = 0;
    }
    ops_count++;
    // Op 776: Remove pmap 100 va=0x49f12000
    if (pmaps[100]) {
        pmap_remove(pmaps[100], 1240539136);
    }
    ops_count++;
    // Op 777: Create pmap 102
    pmaps[102] = pmap_create();
    if (!pmaps[102]) kprint("Warning: pmap_create failed for 102\n");
    ops_count++;
    // Op 778: Enter pmap 99 va=0x9e91b000 pa=0xdc97000 prot=0x5
    if (pmaps[99]) {
        pmap_enter(pmaps[99], 2660347904, 231305216, 5, 0);
    }
    ops_count++;
    // Op 779: Extract pmap 100 va=0x624a3000
    if (pmaps[100]) {
        pmap_extract(pmaps[100], 1649029120);
    }
    ops_count++;
    // Op 780: Extract pmap 102 va=0x68bd1000
    if (pmaps[102]) {
        pmap_extract(pmaps[102], 1757220864);
    }
    ops_count++;
    // Op 781: Remove pmap 98 va=0x1d2c3000
    if (pmaps[98]) {
        pmap_remove(pmaps[98], 489435136);
    }
    ops_count++;
    // Op 782: Destroy pmap 42
    if (pmaps[42]) {
        pmap_destroy(pmaps[42]);
        pmaps[42] = 0;
    }
    ops_count++;
    // Op 783: Protect pmap 91 va=0xa518b000
    if (pmaps[91]) {
        pmap_protect(pmaps[91], 2769858560, 2769862656, 1);
    }
    ops_count++;
    // Op 784: Enter pmap 95 va=0x2b0fc000 pa=0xc7ee9000 prot=0xf
    if (pmaps[95]) {
        pmap_enter(pmaps[95], 722452480, 3354300416, 15, 0);
    }
    ops_count++;
    // Op 785: Remove pmap 102 va=0x8cbae000
    if (pmaps[102]) {
        pmap_remove(pmaps[102], 2361057280);
    }
    ops_count++;
    // Op 786: Enter pmap 79 va=0x44946000 pa=0xc67dc000 prot=0x5
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 1150574592, 3330129920, 5, 0);
    }
    ops_count++;
    // Op 787: Enter pmap 75 va=0xb80f2000 pa=0x710d6000 prot=0x3
    if (pmaps[75]) {
        pmap_enter(pmaps[75], 3087998976, 1896701952, 3, 0);
    }
    ops_count++;
    // Op 788: Extract pmap 79 va=0x44946000
    if (pmaps[79]) {
        pmap_extract(pmaps[79], 1150574592);
    }
    ops_count++;
    // Op 789: Protect pmap 95 va=0x2b0fc000
    if (pmaps[95]) {
        pmap_protect(pmaps[95], 722452480, 722456576, 15);
    }
    ops_count++;
    // Op 790: Enter pmap 78 va=0x7066f000 pa=0xdd93a000 prot=0x1
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 1885794304, 3717439488, 1, 0);
    }
    ops_count++;
    // Op 791: Protect pmap 91 va=0x23d3d000
    if (pmaps[91]) {
        pmap_protect(pmaps[91], 601083904, 601088000, 15);
    }
    ops_count++;
    // Op 792: Extract pmap 99 va=0x9e91b000
    if (pmaps[99]) {
        pmap_extract(pmaps[99], 2660347904);
    }
    ops_count++;
    // Op 793: Protect pmap 89 va=0x774c1000
    if (pmaps[89]) {
        pmap_protect(pmaps[89], 2001473536, 2001477632, 15);
    }
    ops_count++;
    // Op 794: Enter pmap 88 va=0x52db0000 pa=0xfaafc000 prot=0x5
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 1390084096, 4205821952, 5, 0);
    }
    ops_count++;
    // Op 795: Enter pmap 94 va=0x83b0c000 pa=0xfc58f000 prot=0x1
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 2209398784, 4233687040, 1, 0);
    }
    ops_count++;
    // Op 796: Create pmap 103
    pmaps[103] = pmap_create();
    if (!pmaps[103]) kprint("Warning: pmap_create failed for 103\n");
    ops_count++;
    // Op 797: Protect pmap 99 va=0x9e91b000
    if (pmaps[99]) {
        pmap_protect(pmaps[99], 2660347904, 2660352000, 15);
    }
    ops_count++;
    // Op 798: Extract pmap 87 va=0x8567d000
    if (pmaps[87]) {
        pmap_extract(pmaps[87], 2238173184);
    }
    ops_count++;
    // Op 799: Destroy pmap 96
    if (pmaps[96]) {
        pmap_destroy(pmaps[96]);
        pmaps[96] = 0;
    }
    ops_count++;
    kprint(".");
    // Op 800: Protect pmap 99 va=0x9e91b000
    if (pmaps[99]) {
        pmap_protect(pmaps[99], 2660347904, 2660352000, 1);
    }
    ops_count++;
    // Op 801: Remove pmap 94 va=0x98d32000
    if (pmaps[94]) {
        pmap_remove(pmaps[94], 2563973120);
    }
    ops_count++;
    // Op 802: Enter pmap 78 va=0xa2357000 pa=0xcb7ad000 prot=0x1
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 2721411072, 3413823488, 1, 0);
    }
    ops_count++;
    // Op 803: Enter pmap 94 va=0xb1d82000 pa=0xefc3b000 prot=0x5
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 2983731200, 4022579200, 5, 0);
    }
    ops_count++;
    // Op 804: Enter pmap 103 va=0x53c94000 pa=0x950b0000 prot=0x1
    if (pmaps[103]) {
        pmap_enter(pmaps[103], 1405698048, 2500526080, 1, 0);
    }
    ops_count++;
    // Op 805: Extract pmap 78 va=0xa2357000
    if (pmaps[78]) {
        pmap_extract(pmaps[78], 2721411072);
    }
    ops_count++;
    // Op 806: Remove pmap 98 va=0xb8d4a000
    if (pmaps[98]) {
        pmap_remove(pmaps[98], 3100942336);
    }
    ops_count++;
    // Op 807: Protect pmap 99 va=0x9e91b000
    if (pmaps[99]) {
        pmap_protect(pmaps[99], 2660347904, 2660352000, 15);
    }
    ops_count++;
    // Op 808: Protect pmap 103 va=0x53c94000
    if (pmaps[103]) {
        pmap_protect(pmaps[103], 1405698048, 1405702144, 15);
    }
    ops_count++;
    // Op 809: Protect pmap 97 va=0x13b12000
    if (pmaps[97]) {
        pmap_protect(pmaps[97], 330375168, 330379264, 15);
    }
    ops_count++;
    // Op 810: Protect pmap 75 va=0xb80f2000
    if (pmaps[75]) {
        pmap_protect(pmaps[75], 3087998976, 3088003072, 1);
    }
    ops_count++;
    // Op 811: Remove pmap 102 va=0x42b1000
    if (pmaps[102]) {
        pmap_remove(pmaps[102], 69931008);
    }
    ops_count++;
    // Op 812: Create pmap 104
    pmaps[104] = pmap_create();
    if (!pmaps[104]) kprint("Warning: pmap_create failed for 104\n");
    ops_count++;
    // Op 813: Extract pmap 102 va=0x6d845000
    if (pmaps[102]) {
        pmap_extract(pmaps[102], 1837387776);
    }
    ops_count++;
    // Op 814: Destroy pmap 87
    if (pmaps[87]) {
        pmap_destroy(pmaps[87]);
        pmaps[87] = 0;
    }
    ops_count++;
    // Op 815: Extract pmap 97 va=0x936ce000
    if (pmaps[97]) {
        pmap_extract(pmaps[97], 2473385984);
    }
    ops_count++;
    // Op 816: Create pmap 105
    pmaps[105] = pmap_create();
    if (!pmaps[105]) kprint("Warning: pmap_create failed for 105\n");
    ops_count++;
    // Op 817: Enter pmap 78 va=0x5f313000 pa=0xaa2d3000 prot=0x3
    if (pmaps[78]) {
        pmap_enter(pmaps[78], 1597059072, 2855088128, 3, 0);
    }
    ops_count++;
    // Op 818: Create pmap 106
    pmaps[106] = pmap_create();
    if (!pmaps[106]) kprint("Warning: pmap_create failed for 106\n");
    ops_count++;
    // Op 819: Remove pmap 94 va=0xb1d82000
    if (pmaps[94]) {
        pmap_remove(pmaps[94], 2983731200);
    }
    ops_count++;
    // Op 820: Remove pmap 79 va=0x31228000
    if (pmaps[79]) {
        pmap_remove(pmaps[79], 824344576);
    }
    ops_count++;
    // Op 821: Destroy pmap 78
    if (pmaps[78]) {
        pmap_destroy(pmaps[78]);
        pmaps[78] = 0;
    }
    ops_count++;
    // Op 822: Create pmap 107
    pmaps[107] = pmap_create();
    if (!pmaps[107]) kprint("Warning: pmap_create failed for 107\n");
    ops_count++;
    // Op 823: Destroy pmap 104
    if (pmaps[104]) {
        pmap_destroy(pmaps[104]);
        pmaps[104] = 0;
    }
    ops_count++;
    // Op 824: Create pmap 108
    pmaps[108] = pmap_create();
    if (!pmaps[108]) kprint("Warning: pmap_create failed for 108\n");
    ops_count++;
    // Op 825: Remove pmap 88 va=0x52db0000
    if (pmaps[88]) {
        pmap_remove(pmaps[88], 1390084096);
    }
    ops_count++;
    // Op 826: Enter pmap 89 va=0x7a4aa000 pa=0x1231c000 prot=0x3
    if (pmaps[89]) {
        pmap_enter(pmaps[89], 2051710976, 305250304, 3, 0);
    }
    ops_count++;
    // Op 827: Enter pmap 88 va=0x1a6b7000 pa=0x5047c000 prot=0x5
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 443248640, 1346879488, 5, 0);
    }
    ops_count++;
    // Op 828: Extract pmap 89 va=0x7580f000
    if (pmaps[89]) {
        pmap_extract(pmaps[89], 1971384320);
    }
    ops_count++;
    // Op 829: Remove pmap 89 va=0x774c1000
    if (pmaps[89]) {
        pmap_remove(pmaps[89], 2001473536);
    }
    ops_count++;
    // Op 830: Enter pmap 66 va=0x9c3df000 pa=0xa677b000 prot=0x3
    if (pmaps[66]) {
        pmap_enter(pmaps[66], 2621304832, 2792861696, 3, 0);
    }
    ops_count++;
    // Op 831: Enter pmap 94 va=0xa0c9e000 pa=0xbf9e8000 prot=0x3
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 2697584640, 3214835712, 3, 0);
    }
    ops_count++;
    // Op 832: Extract pmap 105 va=0x16313000
    if (pmaps[105]) {
        pmap_extract(pmaps[105], 372322304);
    }
    ops_count++;
    // Op 833: Extract pmap 89 va=0x8d7de000
    if (pmaps[89]) {
        pmap_extract(pmaps[89], 2373836800);
    }
    ops_count++;
    // Op 834: Extract pmap 99 va=0x9e91b000
    if (pmaps[99]) {
        pmap_extract(pmaps[99], 2660347904);
    }
    ops_count++;
    // Op 835: Enter pmap 79 va=0x3f87e000 pa=0x8dbb6000 prot=0x5
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 1065869312, 2377867264, 5, 0);
    }
    ops_count++;
    // Op 836: Protect pmap 66 va=0x9c3df000
    if (pmaps[66]) {
        pmap_protect(pmaps[66], 2621304832, 2621308928, 15);
    }
    ops_count++;
    // Op 837: Destroy pmap 107
    if (pmaps[107]) {
        pmap_destroy(pmaps[107]);
        pmaps[107] = 0;
    }
    ops_count++;
    // Op 838: Destroy pmap 102
    if (pmaps[102]) {
        pmap_destroy(pmaps[102]);
        pmaps[102] = 0;
    }
    ops_count++;
    // Op 839: Enter pmap 75 va=0x3d458000 pa=0x7ec0f000 prot=0xf
    if (pmaps[75]) {
        pmap_enter(pmaps[75], 1027964928, 2126573568, 15, 0);
    }
    ops_count++;
    // Op 840: Create pmap 109
    pmaps[109] = pmap_create();
    if (!pmaps[109]) kprint("Warning: pmap_create failed for 109\n");
    ops_count++;
    // Op 841: Enter pmap 79 va=0x873dd000 pa=0xe3937000 prot=0x1
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 2268975104, 3818090496, 1, 0);
    }
    ops_count++;
    // Op 842: Extract pmap 99 va=0x9e91b000
    if (pmaps[99]) {
        pmap_extract(pmaps[99], 2660347904);
    }
    ops_count++;
    // Op 843: Enter pmap 91 va=0x78947000 pa=0xf4670000 prot=0x1
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 2022993920, 4100390912, 1, 0);
    }
    ops_count++;
    // Op 844: Create pmap 110
    pmaps[110] = pmap_create();
    if (!pmaps[110]) kprint("Warning: pmap_create failed for 110\n");
    ops_count++;
    // Op 845: Create pmap 111
    pmaps[111] = pmap_create();
    if (!pmaps[111]) kprint("Warning: pmap_create failed for 111\n");
    ops_count++;
    // Op 846: Enter pmap 75 va=0xf7af000 pa=0xf2ecb000 prot=0xf
    if (pmaps[75]) {
        pmap_enter(pmaps[75], 259715072, 4075597824, 15, 0);
    }
    ops_count++;
    // Op 847: Enter pmap 109 va=0x6a59d000 pa=0x6c7f6000 prot=0xf
    if (pmaps[109]) {
        pmap_enter(pmaps[109], 1784270848, 1820286976, 15, 0);
    }
    ops_count++;
    // Op 848: Create pmap 112
    pmaps[112] = pmap_create();
    if (!pmaps[112]) kprint("Warning: pmap_create failed for 112\n");
    ops_count++;
    // Op 849: Enter pmap 112 va=0x27162000 pa=0x47182000 prot=0x1
    if (pmaps[112]) {
        pmap_enter(pmaps[112], 655761408, 1192763392, 1, 0);
    }
    ops_count++;
    // Op 850: Enter pmap 109 va=0x3404d000 pa=0xf97c9000 prot=0x3
    if (pmaps[109]) {
        pmap_enter(pmaps[109], 872730624, 4185690112, 3, 0);
    }
    ops_count++;
    // Op 851: Enter pmap 103 va=0x9620c000 pa=0xa2711000 prot=0x1
    if (pmaps[103]) {
        pmap_enter(pmaps[103], 2518728704, 2725318656, 1, 0);
    }
    ops_count++;
    // Op 852: Enter pmap 105 va=0xbade9000 pa=0xf423d000 prot=0x3
    if (pmaps[105]) {
        pmap_enter(pmaps[105], 3135148032, 4095987712, 3, 0);
    }
    ops_count++;
    // Op 853: Create pmap 113
    pmaps[113] = pmap_create();
    if (!pmaps[113]) kprint("Warning: pmap_create failed for 113\n");
    ops_count++;
    // Op 854: Create pmap 114
    pmaps[114] = pmap_create();
    if (!pmaps[114]) kprint("Warning: pmap_create failed for 114\n");
    ops_count++;
    // Op 855: Enter pmap 106 va=0x9cbf8000 pa=0x9bec7000 prot=0x1
    if (pmaps[106]) {
        pmap_enter(pmaps[106], 2629795840, 2615963648, 1, 0);
    }
    ops_count++;
    // Op 856: Enter pmap 114 va=0xbd9bd000 pa=0x2d072000 prot=0x1
    if (pmaps[114]) {
        pmap_enter(pmaps[114], 3181105152, 755441664, 1, 0);
    }
    ops_count++;
    // Op 857: Create pmap 115
    pmaps[115] = pmap_create();
    if (!pmaps[115]) kprint("Warning: pmap_create failed for 115\n");
    ops_count++;
    // Op 858: Create pmap 116
    pmaps[116] = pmap_create();
    if (!pmaps[116]) kprint("Warning: pmap_create failed for 116\n");
    ops_count++;
    // Op 859: Create pmap 117
    pmaps[117] = pmap_create();
    if (!pmaps[117]) kprint("Warning: pmap_create failed for 117\n");
    ops_count++;
    // Op 860: Enter pmap 100 va=0x85e3b000 pa=0x8478b000 prot=0x5
    if (pmaps[100]) {
        pmap_enter(pmaps[100], 2246291456, 2222501888, 5, 0);
    }
    ops_count++;
    // Op 861: Destroy pmap 89
    if (pmaps[89]) {
        pmap_destroy(pmaps[89]);
        pmaps[89] = 0;
    }
    ops_count++;
    // Op 862: Protect pmap 98 va=0x3b5af000
    if (pmaps[98]) {
        pmap_protect(pmaps[98], 995815424, 995819520, 1);
    }
    ops_count++;
    // Op 863: Enter pmap 106 va=0x45a09000 pa=0x862b4000 prot=0x1
    if (pmaps[106]) {
        pmap_enter(pmaps[106], 1168150528, 2250981376, 1, 0);
    }
    ops_count++;
    // Op 864: Enter pmap 112 va=0xa0a60000 pa=0x13cad000 prot=0xf
    if (pmaps[112]) {
        pmap_enter(pmaps[112], 2695233536, 332058624, 15, 0);
    }
    ops_count++;
    // Op 865: Enter pmap 103 va=0xe315000 pa=0x7ff67000 prot=0x3
    if (pmaps[103]) {
        pmap_enter(pmaps[103], 238112768, 2146856960, 3, 0);
    }
    ops_count++;
    // Op 866: Enter pmap 98 va=0x1a580000 pa=0xe4fec000 prot=0x1
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 441974784, 3841900544, 1, 0);
    }
    ops_count++;
    // Op 867: Destroy pmap 110
    if (pmaps[110]) {
        pmap_destroy(pmaps[110]);
        pmaps[110] = 0;
    }
    ops_count++;
    // Op 868: Create pmap 118
    pmaps[118] = pmap_create();
    if (!pmaps[118]) kprint("Warning: pmap_create failed for 118\n");
    ops_count++;
    // Op 869: Extract pmap 66 va=0x98d01000
    if (pmaps[66]) {
        pmap_extract(pmaps[66], 2563772416);
    }
    ops_count++;
    // Op 870: Enter pmap 91 va=0x2dd07000 pa=0xee708000 prot=0x1
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 768634880, 4000350208, 1, 0);
    }
    ops_count++;
    // Op 871: Remove pmap 117 va=0xbd9c5000
    if (pmaps[117]) {
        pmap_remove(pmaps[117], 3181137920);
    }
    ops_count++;
    // Op 872: Remove pmap 97 va=0x936ce000
    if (pmaps[97]) {
        pmap_remove(pmaps[97], 2473385984);
    }
    ops_count++;
    // Op 873: Destroy pmap 118
    if (pmaps[118]) {
        pmap_destroy(pmaps[118]);
        pmaps[118] = 0;
    }
    ops_count++;
    // Op 874: Enter pmap 79 va=0x92503000 pa=0x5f805000 prot=0x1
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 2454728704, 1602244608, 1, 0);
    }
    ops_count++;
    // Op 875: Enter pmap 75 va=0x2de99000 pa=0x3932a000 prot=0x1
    if (pmaps[75]) {
        pmap_enter(pmaps[75], 770281472, 959619072, 1, 0);
    }
    ops_count++;
    // Op 876: Extract pmap 79 va=0x48455000
    if (pmaps[79]) {
        pmap_extract(pmaps[79], 1212502016);
    }
    ops_count++;
    // Op 877: Protect pmap 99 va=0x9e91b000
    if (pmaps[99]) {
        pmap_protect(pmaps[99], 2660347904, 2660352000, 1);
    }
    ops_count++;
    // Op 878: Remove pmap 66 va=0x3818d000
    if (pmaps[66]) {
        pmap_remove(pmaps[66], 941150208);
    }
    ops_count++;
    // Op 879: Destroy pmap 113
    if (pmaps[113]) {
        pmap_destroy(pmaps[113]);
        pmaps[113] = 0;
    }
    ops_count++;
    // Op 880: Destroy pmap 103
    if (pmaps[103]) {
        pmap_destroy(pmaps[103]);
        pmaps[103] = 0;
    }
    ops_count++;
    // Op 881: Remove pmap 111 va=0x32074000
    if (pmaps[111]) {
        pmap_remove(pmaps[111], 839335936);
    }
    ops_count++;
    // Op 882: Protect pmap 114 va=0xbd9bd000
    if (pmaps[114]) {
        pmap_protect(pmaps[114], 3181105152, 3181109248, 1);
    }
    ops_count++;
    // Op 883: Create pmap 119
    pmaps[119] = pmap_create();
    if (!pmaps[119]) kprint("Warning: pmap_create failed for 119\n");
    ops_count++;
    // Op 884: Enter pmap 97 va=0x8aa2c000 pa=0x58bf3000 prot=0x1
    if (pmaps[97]) {
        pmap_enter(pmaps[97], 2325921792, 1488924672, 1, 0);
    }
    ops_count++;
    // Op 885: Extract pmap 66 va=0x1bfa000
    if (pmaps[66]) {
        pmap_extract(pmaps[66], 29335552);
    }
    ops_count++;
    // Op 886: Protect pmap 99 va=0x9e91b000
    if (pmaps[99]) {
        pmap_protect(pmaps[99], 2660347904, 2660352000, 1);
    }
    ops_count++;
    // Op 887: Enter pmap 97 va=0x6b791000 pa=0x7e059000 prot=0x1
    if (pmaps[97]) {
        pmap_enter(pmaps[97], 1803096064, 2114293760, 1, 0);
    }
    ops_count++;
    // Op 888: Extract pmap 91 va=0x9a592000
    if (pmaps[91]) {
        pmap_extract(pmaps[91], 2589532160);
    }
    ops_count++;
    // Op 889: Create pmap 120
    pmaps[120] = pmap_create();
    if (!pmaps[120]) kprint("Warning: pmap_create failed for 120\n");
    ops_count++;
    // Op 890: Destroy pmap 112
    if (pmaps[112]) {
        pmap_destroy(pmaps[112]);
        pmaps[112] = 0;
    }
    ops_count++;
    // Op 891: Enter pmap 105 va=0x755d9000 pa=0xf98c6000 prot=0x1
    if (pmaps[105]) {
        pmap_enter(pmaps[105], 1969065984, 4186726400, 1, 0);
    }
    ops_count++;
    // Op 892: Remove pmap 114 va=0xbd9bd000
    if (pmaps[114]) {
        pmap_remove(pmaps[114], 3181105152);
    }
    ops_count++;
    // Op 893: Remove pmap 106 va=0x9cbf8000
    if (pmaps[106]) {
        pmap_remove(pmaps[106], 2629795840);
    }
    ops_count++;
    // Op 894: Remove pmap 75 va=0x2de99000
    if (pmaps[75]) {
        pmap_remove(pmaps[75], 770281472);
    }
    ops_count++;
    // Op 895: Remove pmap 91 va=0x23d3d000
    if (pmaps[91]) {
        pmap_remove(pmaps[91], 601083904);
    }
    ops_count++;
    // Op 896: Enter pmap 100 va=0x64989000 pa=0xb372c000 prot=0x5
    if (pmaps[100]) {
        pmap_enter(pmaps[100], 1687719936, 3010641920, 5, 0);
    }
    ops_count++;
    // Op 897: Enter pmap 98 va=0x3bce0000 pa=0x132eb000 prot=0x3
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 1003356160, 321826816, 3, 0);
    }
    ops_count++;
    // Op 898: Create pmap 121
    pmaps[121] = pmap_create();
    if (!pmaps[121]) kprint("Warning: pmap_create failed for 121\n");
    ops_count++;
    // Op 899: Protect pmap 88 va=0xf79b000
    if (pmaps[88]) {
        pmap_protect(pmaps[88], 259633152, 259637248, 1);
    }
    ops_count++;
    kprint(".");
    // Op 900: Destroy pmap 109
    if (pmaps[109]) {
        pmap_destroy(pmaps[109]);
        pmaps[109] = 0;
    }
    ops_count++;
    // Op 901: Enter pmap 88 va=0x89326000 pa=0x5bc8b000 prot=0x3
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 2301779968, 1539878912, 3, 0);
    }
    ops_count++;
    // Op 902: Enter pmap 111 va=0x43d79000 pa=0xda4c4000 prot=0x1
    if (pmaps[111]) {
        pmap_enter(pmaps[111], 1138200576, 3662430208, 1, 0);
    }
    ops_count++;
    // Op 903: Create pmap 122
    pmaps[122] = pmap_create();
    if (!pmaps[122]) kprint("Warning: pmap_create failed for 122\n");
    ops_count++;
    // Op 904: Protect pmap 99 va=0x9e91b000
    if (pmaps[99]) {
        pmap_protect(pmaps[99], 2660347904, 2660352000, 1);
    }
    ops_count++;
    // Op 905: Protect pmap 100 va=0x85e3b000
    if (pmaps[100]) {
        pmap_protect(pmaps[100], 2246291456, 2246295552, 1);
    }
    ops_count++;
    // Op 906: Protect pmap 105 va=0x755d9000
    if (pmaps[105]) {
        pmap_protect(pmaps[105], 1969065984, 1969070080, 1);
    }
    ops_count++;
    // Op 907: Remove pmap 98 va=0x1a580000
    if (pmaps[98]) {
        pmap_remove(pmaps[98], 441974784);
    }
    ops_count++;
    // Op 908: Create pmap 123
    pmaps[123] = pmap_create();
    if (!pmaps[123]) kprint("Warning: pmap_create failed for 123\n");
    ops_count++;
    // Op 909: Enter pmap 111 va=0x8e530000 pa=0xbf921000 prot=0x3
    if (pmaps[111]) {
        pmap_enter(pmaps[111], 2387804160, 3214020608, 3, 0);
    }
    ops_count++;
    // Op 910: Create pmap 124
    pmaps[124] = pmap_create();
    if (!pmaps[124]) kprint("Warning: pmap_create failed for 124\n");
    ops_count++;
    // Op 911: Create pmap 125
    pmaps[125] = pmap_create();
    if (!pmaps[125]) kprint("Warning: pmap_create failed for 125\n");
    ops_count++;
    // Op 912: Protect pmap 88 va=0x97778000
    if (pmaps[88]) {
        pmap_protect(pmaps[88], 2541191168, 2541195264, 1);
    }
    ops_count++;
    // Op 913: Protect pmap 97 va=0x13b12000
    if (pmaps[97]) {
        pmap_protect(pmaps[97], 330375168, 330379264, 1);
    }
    ops_count++;
    // Op 914: Protect pmap 105 va=0xbade9000
    if (pmaps[105]) {
        pmap_protect(pmaps[105], 3135148032, 3135152128, 15);
    }
    ops_count++;
    // Op 915: Extract pmap 79 va=0x11666000
    if (pmaps[79]) {
        pmap_extract(pmaps[79], 291921920);
    }
    ops_count++;
    // Op 916: Destroy pmap 120
    if (pmaps[120]) {
        pmap_destroy(pmaps[120]);
        pmaps[120] = 0;
    }
    ops_count++;
    // Op 917: Enter pmap 91 va=0x178bf000 pa=0xe7c33000 prot=0x5
    if (pmaps[91]) {
        pmap_enter(pmaps[91], 395046912, 3888328704, 5, 0);
    }
    ops_count++;
    // Op 918: Create pmap 126
    pmaps[126] = pmap_create();
    if (!pmaps[126]) kprint("Warning: pmap_create failed for 126\n");
    ops_count++;
    // Op 919: Extract pmap 75 va=0xf7af000
    if (pmaps[75]) {
        pmap_extract(pmaps[75], 259715072);
    }
    ops_count++;
    // Op 920: Enter pmap 124 va=0x651eb000 pa=0x7f122000 prot=0x1
    if (pmaps[124]) {
        pmap_enter(pmaps[124], 1696509952, 2131894272, 1, 0);
    }
    ops_count++;
    // Op 921: Enter pmap 108 va=0x2c3d2000 pa=0x5aac0000 prot=0x3
    if (pmaps[108]) {
        pmap_enter(pmaps[108], 742203392, 1521221632, 3, 0);
    }
    ops_count++;
    // Op 922: Extract pmap 98 va=0x3b5af000
    if (pmaps[98]) {
        pmap_extract(pmaps[98], 995815424);
    }
    ops_count++;
    // Op 923: Create pmap 127
    pmaps[127] = pmap_create();
    if (!pmaps[127]) kprint("Warning: pmap_create failed for 127\n");
    ops_count++;
    // Op 924: Remove pmap 97 va=0x6b791000
    if (pmaps[97]) {
        pmap_remove(pmaps[97], 1803096064);
    }
    ops_count++;
    // Op 925: Enter pmap 108 va=0x8cf9f000 pa=0x7ab1c000 prot=0x1
    if (pmaps[108]) {
        pmap_enter(pmaps[108], 2365190144, 2058469376, 1, 0);
    }
    ops_count++;
    // Op 926: Create pmap 128
    pmaps[128] = pmap_create();
    if (!pmaps[128]) kprint("Warning: pmap_create failed for 128\n");
    ops_count++;
    // Op 927: Enter pmap 108 va=0x330fb000 pa=0xa494e000 prot=0x3
    if (pmaps[108]) {
        pmap_enter(pmaps[108], 856666112, 2761220096, 3, 0);
    }
    ops_count++;
    // Op 928: Remove pmap 126 va=0x8b2d2000
    if (pmaps[126]) {
        pmap_remove(pmaps[126], 2334990336);
    }
    ops_count++;
    // Op 929: Protect pmap 106 va=0x45a09000
    if (pmaps[106]) {
        pmap_protect(pmaps[106], 1168150528, 1168154624, 15);
    }
    ops_count++;
    // Op 930: Enter pmap 114 va=0x5812a000 pa=0xe9b53000 prot=0x5
    if (pmaps[114]) {
        pmap_enter(pmaps[114], 1477615616, 3920965632, 5, 0);
    }
    ops_count++;
    // Op 931: Enter pmap 94 va=0x6ac2000 pa=0x51b1f000 prot=0x3
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 111943680, 1370615808, 3, 0);
    }
    ops_count++;
    // Op 932: Create pmap 129
    pmaps[129] = pmap_create();
    if (!pmaps[129]) kprint("Warning: pmap_create failed for 129\n");
    ops_count++;
    // Op 933: Protect pmap 98 va=0x3b5af000
    if (pmaps[98]) {
        pmap_protect(pmaps[98], 995815424, 995819520, 15);
    }
    ops_count++;
    // Op 934: Enter pmap 125 va=0x95805000 pa=0xfff62000 prot=0x3
    if (pmaps[125]) {
        pmap_enter(pmaps[125], 2508214272, 4294320128, 3, 0);
    }
    ops_count++;
    // Op 935: Extract pmap 88 va=0x721e9000
    if (pmaps[88]) {
        pmap_extract(pmaps[88], 1914605568);
    }
    ops_count++;
    // Op 936: Enter pmap 127 va=0xbcb00000 pa=0xe600000 prot=0x3
    if (pmaps[127]) {
        pmap_enter(pmaps[127], 3165650944, 241172480, 3, 0);
    }
    ops_count++;
    // Op 937: Remove pmap 126 va=0x636fb000
    if (pmaps[126]) {
        pmap_remove(pmaps[126], 1668263936);
    }
    ops_count++;
    // Op 938: Enter pmap 127 va=0x2c787000 pa=0x2d24e000 prot=0x3
    if (pmaps[127]) {
        pmap_enter(pmaps[127], 746090496, 757391360, 3, 0);
    }
    ops_count++;
    // Op 939: Remove pmap 126 va=0xbcf48000
    if (pmaps[126]) {
        pmap_remove(pmaps[126], 3170140160);
    }
    ops_count++;
    // Op 940: Remove pmap 117 va=0x7389000
    if (pmaps[117]) {
        pmap_remove(pmaps[117], 121147392);
    }
    ops_count++;
    // Op 941: Protect pmap 105 va=0xbade9000
    if (pmaps[105]) {
        pmap_protect(pmaps[105], 3135148032, 3135152128, 1);
    }
    ops_count++;
    // Op 942: Extract pmap 114 va=0x5812a000
    if (pmaps[114]) {
        pmap_extract(pmaps[114], 1477615616);
    }
    ops_count++;
    // Op 943: Enter pmap 79 va=0xe611000 pa=0x3b9d3000 prot=0x3
    if (pmaps[79]) {
        pmap_enter(pmaps[79], 241242112, 1000157184, 3, 0);
    }
    ops_count++;
    // Op 944: Create pmap 130
    pmaps[130] = pmap_create();
    if (!pmaps[130]) kprint("Warning: pmap_create failed for 130\n");
    ops_count++;
    // Op 945: Enter pmap 100 va=0x9d37f000 pa=0x33a32000 prot=0x3
    if (pmaps[100]) {
        pmap_enter(pmaps[100], 2637688832, 866328576, 3, 0);
    }
    ops_count++;
    // Op 946: Enter pmap 94 va=0x52d6c000 pa=0xf4f0000 prot=0x1
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 1389805568, 256835584, 1, 0);
    }
    ops_count++;
    // Op 947: Create pmap 131
    pmaps[131] = pmap_create();
    if (!pmaps[131]) kprint("Warning: pmap_create failed for 131\n");
    ops_count++;
    // Op 948: Protect pmap 91 va=0x9a592000
    if (pmaps[91]) {
        pmap_protect(pmaps[91], 2589532160, 2589536256, 15);
    }
    ops_count++;
    // Op 949: Extract pmap 105 va=0x755d9000
    if (pmaps[105]) {
        pmap_extract(pmaps[105], 1969065984);
    }
    ops_count++;
    // Op 950: Remove pmap 100 va=0x64989000
    if (pmaps[100]) {
        pmap_remove(pmaps[100], 1687719936);
    }
    ops_count++;
    // Op 951: Enter pmap 94 va=0xbb7b7000 pa=0xf2ed9000 prot=0xf
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 3145428992, 4075655168, 15, 0);
    }
    ops_count++;
    // Op 952: Enter pmap 100 va=0x2c989000 pa=0xb519f000 prot=0x5
    if (pmaps[100]) {
        pmap_enter(pmaps[100], 748195840, 3038375936, 5, 0);
    }
    ops_count++;
    // Op 953: Destroy pmap 117
    if (pmaps[117]) {
        pmap_destroy(pmaps[117]);
        pmaps[117] = 0;
    }
    ops_count++;
    // Op 954: Protect pmap 106 va=0x45a09000
    if (pmaps[106]) {
        pmap_protect(pmaps[106], 1168150528, 1168154624, 15);
    }
    ops_count++;
    // Op 955: Create pmap 132
    pmaps[132] = pmap_create();
    if (!pmaps[132]) kprint("Warning: pmap_create failed for 132\n");
    ops_count++;
    // Op 956: Enter pmap 126 va=0x91469000 pa=0x85a3000 prot=0x3
    if (pmaps[126]) {
        pmap_enter(pmaps[126], 2437320704, 140128256, 3, 0);
    }
    ops_count++;
    // Op 957: Remove pmap 131 va=0x9c838000
    if (pmaps[131]) {
        pmap_remove(pmaps[131], 2625863680);
    }
    ops_count++;
    // Op 958: Destroy pmap 119
    if (pmaps[119]) {
        pmap_destroy(pmaps[119]);
        pmaps[119] = 0;
    }
    ops_count++;
    // Op 959: Create pmap 133
    pmaps[133] = pmap_create();
    if (!pmaps[133]) kprint("Warning: pmap_create failed for 133\n");
    ops_count++;
    // Op 960: Enter pmap 100 va=0xbecee000 pa=0x13fb2000 prot=0x3
    if (pmaps[100]) {
        pmap_enter(pmaps[100], 3201228800, 335224832, 3, 0);
    }
    ops_count++;
    // Op 961: Enter pmap 100 va=0x2afab000 pa=0xa2ed2000 prot=0x1
    if (pmaps[100]) {
        pmap_enter(pmaps[100], 721072128, 2733449216, 1, 0);
    }
    ops_count++;
    // Op 962: Protect pmap 124 va=0x651eb000
    if (pmaps[124]) {
        pmap_protect(pmaps[124], 1696509952, 1696514048, 15);
    }
    ops_count++;
    // Op 963: Create pmap 134
    pmaps[134] = pmap_create();
    if (!pmaps[134]) kprint("Warning: pmap_create failed for 134\n");
    ops_count++;
    // Op 964: Destroy pmap 91
    if (pmaps[91]) {
        pmap_destroy(pmaps[91]);
        pmaps[91] = 0;
    }
    ops_count++;
    // Op 965: Enter pmap 129 va=0x1ba83000 pa=0xb87d0000 prot=0x1
    if (pmaps[129]) {
        pmap_enter(pmaps[129], 464007168, 3095199744, 1, 0);
    }
    ops_count++;
    // Op 966: Create pmap 135
    pmaps[135] = pmap_create();
    if (!pmaps[135]) kprint("Warning: pmap_create failed for 135\n");
    ops_count++;
    // Op 967: Enter pmap 130 va=0x2a953000 pa=0xe281f000 prot=0xf
    if (pmaps[130]) {
        pmap_enter(pmaps[130], 714420224, 3800166400, 15, 0);
    }
    ops_count++;
    // Op 968: Enter pmap 128 va=0x59ea5000 pa=0x8bd83000 prot=0xf
    if (pmaps[128]) {
        pmap_enter(pmaps[128], 1508528128, 2346201088, 15, 0);
    }
    ops_count++;
    // Op 969: Create pmap 136
    pmaps[136] = pmap_create();
    if (!pmaps[136]) kprint("Warning: pmap_create failed for 136\n");
    ops_count++;
    // Op 970: Enter pmap 114 va=0x29a3e000 pa=0x73979000 prot=0x1
    if (pmaps[114]) {
        pmap_enter(pmaps[114], 698605568, 1939312640, 1, 0);
    }
    ops_count++;
    // Op 971: Protect pmap 79 va=0x584fd000
    if (pmaps[79]) {
        pmap_protect(pmaps[79], 1481625600, 1481629696, 1);
    }
    ops_count++;
    // Op 972: Enter pmap 97 va=0x12fed000 pa=0x5790b000 prot=0x3
    if (pmaps[97]) {
        pmap_enter(pmaps[97], 318689280, 1469100032, 3, 0);
    }
    ops_count++;
    // Op 973: Extract pmap 105 va=0xbade9000
    if (pmaps[105]) {
        pmap_extract(pmaps[105], 3135148032);
    }
    ops_count++;
    // Op 974: Enter pmap 134 va=0xb45a7000 pa=0x7ff2000 prot=0xf
    if (pmaps[134]) {
        pmap_enter(pmaps[134], 3025825792, 134160384, 15, 0);
    }
    ops_count++;
    // Op 975: Extract pmap 105 va=0x755d9000
    if (pmaps[105]) {
        pmap_extract(pmaps[105], 1969065984);
    }
    ops_count++;
    // Op 976: Destroy pmap 97
    if (pmaps[97]) {
        pmap_destroy(pmaps[97]);
        pmaps[97] = 0;
    }
    ops_count++;
    // Op 977: Enter pmap 111 va=0x68c6a000 pa=0xb28b7000 prot=0x5
    if (pmaps[111]) {
        pmap_enter(pmaps[111], 1757847552, 2995482624, 5, 0);
    }
    ops_count++;
    // Op 978: Remove pmap 124 va=0x651eb000
    if (pmaps[124]) {
        pmap_remove(pmaps[124], 1696509952);
    }
    ops_count++;
    // Op 979: Create pmap 137
    pmaps[137] = pmap_create();
    if (!pmaps[137]) kprint("Warning: pmap_create failed for 137\n");
    ops_count++;
    // Op 980: Enter pmap 136 va=0x2751b000 pa=0xe040000 prot=0x5
    if (pmaps[136]) {
        pmap_enter(pmaps[136], 659664896, 235143168, 5, 0);
    }
    ops_count++;
    // Op 981: Create pmap 138
    pmaps[138] = pmap_create();
    if (!pmaps[138]) kprint("Warning: pmap_create failed for 138\n");
    ops_count++;
    // Op 982: Create pmap 139
    pmaps[139] = pmap_create();
    if (!pmaps[139]) kprint("Warning: pmap_create failed for 139\n");
    ops_count++;
    // Op 983: Destroy pmap 75
    if (pmaps[75]) {
        pmap_destroy(pmaps[75]);
        pmaps[75] = 0;
    }
    ops_count++;
    // Op 984: Destroy pmap 124
    if (pmaps[124]) {
        pmap_destroy(pmaps[124]);
        pmaps[124] = 0;
    }
    ops_count++;
    // Op 985: Extract pmap 137 va=0x4ef04000
    if (pmaps[137]) {
        pmap_extract(pmaps[137], 1324367872);
    }
    ops_count++;
    // Op 986: Enter pmap 122 va=0x1754c000 pa=0xa4615000 prot=0xf
    if (pmaps[122]) {
        pmap_enter(pmaps[122], 391430144, 2757840896, 15, 0);
    }
    ops_count++;
    // Op 987: Destroy pmap 131
    if (pmaps[131]) {
        pmap_destroy(pmaps[131]);
        pmaps[131] = 0;
    }
    ops_count++;
    // Op 988: Enter pmap 136 va=0xb6c1000 pa=0x43775000 prot=0xf
    if (pmaps[136]) {
        pmap_enter(pmaps[136], 191631360, 1131892736, 15, 0);
    }
    ops_count++;
    // Op 989: Enter pmap 116 va=0x44c08000 pa=0x90827000 prot=0x1
    if (pmaps[116]) {
        pmap_enter(pmaps[116], 1153466368, 2424467456, 1, 0);
    }
    ops_count++;
    // Op 990: Destroy pmap 99
    if (pmaps[99]) {
        pmap_destroy(pmaps[99]);
        pmaps[99] = 0;
    }
    ops_count++;
    // Op 991: Create pmap 140
    pmaps[140] = pmap_create();
    if (!pmaps[140]) kprint("Warning: pmap_create failed for 140\n");
    ops_count++;
    // Op 992: Enter pmap 135 va=0x96f85000 pa=0x3a4e4000 prot=0x1
    if (pmaps[135]) {
        pmap_enter(pmaps[135], 2532855808, 978206720, 1, 0);
    }
    ops_count++;
    // Op 993: Protect pmap 135 va=0x96f85000
    if (pmaps[135]) {
        pmap_protect(pmaps[135], 2532855808, 2532859904, 1);
    }
    ops_count++;
    // Op 994: Enter pmap 98 va=0x6511f000 pa=0xe72c6000 prot=0x1
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 1695674368, 3878445056, 1, 0);
    }
    ops_count++;
    // Op 995: Enter pmap 108 va=0x1ecad000 pa=0xf3125000 prot=0xf
    if (pmaps[108]) {
        pmap_enter(pmaps[108], 516608000, 4078063616, 15, 0);
    }
    ops_count++;
    // Op 996: Destroy pmap 111
    if (pmaps[111]) {
        pmap_destroy(pmaps[111]);
        pmaps[111] = 0;
    }
    ops_count++;
    // Op 997: Extract pmap 135 va=0x96f85000
    if (pmaps[135]) {
        pmap_extract(pmaps[135], 2532855808);
    }
    ops_count++;
    // Op 998: Enter pmap 130 va=0x6f75f000 pa=0x33c0f000 prot=0x3
    if (pmaps[130]) {
        pmap_enter(pmaps[130], 1870000128, 868282368, 3, 0);
    }
    ops_count++;
    // Op 999: Protect pmap 106 va=0x45a09000
    if (pmaps[106]) {
        pmap_protect(pmaps[106], 1168150528, 1168154624, 1);
    }
    ops_count++;
    kprint(".");
    // Op 1000: Create pmap 141
    pmaps[141] = pmap_create();
    if (!pmaps[141]) kprint("Warning: pmap_create failed for 141\n");
    ops_count++;
    // Op 1001: Remove pmap 129 va=0x1ba83000
    if (pmaps[129]) {
        pmap_remove(pmaps[129], 464007168);
    }
    ops_count++;
    // Op 1002: Enter pmap 66 va=0xb34e4000 pa=0x9f90b000 prot=0xf
    if (pmaps[66]) {
        pmap_enter(pmaps[66], 3008249856, 2677059584, 15, 0);
    }
    ops_count++;
    // Op 1003: Create pmap 142
    pmaps[142] = pmap_create();
    if (!pmaps[142]) kprint("Warning: pmap_create failed for 142\n");
    ops_count++;
    // Op 1004: Create pmap 143
    pmaps[143] = pmap_create();
    if (!pmaps[143]) kprint("Warning: pmap_create failed for 143\n");
    ops_count++;
    // Op 1005: Remove pmap 100 va=0x85e3b000
    if (pmaps[100]) {
        pmap_remove(pmaps[100], 2246291456);
    }
    ops_count++;
    // Op 1006: Remove pmap 100 va=0xbecee000
    if (pmaps[100]) {
        pmap_remove(pmaps[100], 3201228800);
    }
    ops_count++;
    // Op 1007: Extract pmap 132 va=0x439e8000
    if (pmaps[132]) {
        pmap_extract(pmaps[132], 1134460928);
    }
    ops_count++;
    // Op 1008: Enter pmap 141 va=0x92702000 pa=0x9bf4f000 prot=0xf
    if (pmaps[141]) {
        pmap_enter(pmaps[141], 2456821760, 2616520704, 15, 0);
    }
    ops_count++;
    // Op 1009: Create pmap 144
    pmaps[144] = pmap_create();
    if (!pmaps[144]) kprint("Warning: pmap_create failed for 144\n");
    ops_count++;
    // Op 1010: Enter pmap 108 va=0x9e455000 pa=0x9be0d000 prot=0xf
    if (pmaps[108]) {
        pmap_enter(pmaps[108], 2655342592, 2615201792, 15, 0);
    }
    ops_count++;
    // Op 1011: Create pmap 145
    pmaps[145] = pmap_create();
    if (!pmaps[145]) kprint("Warning: pmap_create failed for 145\n");
    ops_count++;
    // Op 1012: Extract pmap 130 va=0x2a953000
    if (pmaps[130]) {
        pmap_extract(pmaps[130], 714420224);
    }
    ops_count++;
    // Op 1013: Protect pmap 126 va=0x91469000
    if (pmaps[126]) {
        pmap_protect(pmaps[126], 2437320704, 2437324800, 1);
    }
    ops_count++;
    // Op 1014: Enter pmap 128 va=0x4f9a6000 pa=0x47d4c000 prot=0xf
    if (pmaps[128]) {
        pmap_enter(pmaps[128], 1335517184, 1205125120, 15, 0);
    }
    ops_count++;
    // Op 1015: Extract pmap 100 va=0x3a5b7000
    if (pmaps[100]) {
        pmap_extract(pmaps[100], 979070976);
    }
    ops_count++;
    // Op 1016: Create pmap 146
    pmaps[146] = pmap_create();
    if (!pmaps[146]) kprint("Warning: pmap_create failed for 146\n");
    ops_count++;
    // Op 1017: Extract pmap 138 va=0x52247000
    if (pmaps[138]) {
        pmap_extract(pmaps[138], 1378119680);
    }
    ops_count++;
    // Op 1018: Enter pmap 136 va=0x9adff000 pa=0xa5531000 prot=0x1
    if (pmaps[136]) {
        pmap_enter(pmaps[136], 2598367232, 2773684224, 1, 0);
    }
    ops_count++;
    // Op 1019: Destroy pmap 121
    if (pmaps[121]) {
        pmap_destroy(pmaps[121]);
        pmaps[121] = 0;
    }
    ops_count++;
    // Op 1020: Extract pmap 137 va=0x84397000
    if (pmaps[137]) {
        pmap_extract(pmaps[137], 2218356736);
    }
    ops_count++;
    // Op 1021: Extract pmap 94 va=0x83b0c000
    if (pmaps[94]) {
        pmap_extract(pmaps[94], 2209398784);
    }
    ops_count++;
    // Op 1022: Create pmap 147
    pmaps[147] = pmap_create();
    if (!pmaps[147]) kprint("Warning: pmap_create failed for 147\n");
    ops_count++;
    // Op 1023: Protect pmap 136 va=0x2751b000
    if (pmaps[136]) {
        pmap_protect(pmaps[136], 659664896, 659668992, 1);
    }
    ops_count++;
    // Op 1024: Destroy pmap 116
    if (pmaps[116]) {
        pmap_destroy(pmaps[116]);
        pmaps[116] = 0;
    }
    ops_count++;
    // Op 1025: Protect pmap 126 va=0x91469000
    if (pmaps[126]) {
        pmap_protect(pmaps[126], 2437320704, 2437324800, 15);
    }
    ops_count++;
    // Op 1026: Create pmap 148
    pmaps[148] = pmap_create();
    if (!pmaps[148]) kprint("Warning: pmap_create failed for 148\n");
    ops_count++;
    // Op 1027: Enter pmap 122 va=0x46237000 pa=0x5e5c4000 prot=0xf
    if (pmaps[122]) {
        pmap_enter(pmaps[122], 1176727552, 1583104000, 15, 0);
    }
    ops_count++;
    // Op 1028: Enter pmap 105 va=0xae618000 pa=0x7e2f000 prot=0x3
    if (pmaps[105]) {
        pmap_enter(pmaps[105], 2925625344, 132313088, 3, 0);
    }
    ops_count++;
    // Op 1029: Enter pmap 140 va=0x2d762000 pa=0xf5f4a000 prot=0xf
    if (pmaps[140]) {
        pmap_enter(pmaps[140], 762716160, 4126449664, 15, 0);
    }
    ops_count++;
    // Op 1030: Remove pmap 134 va=0xb45a7000
    if (pmaps[134]) {
        pmap_remove(pmaps[134], 3025825792);
    }
    ops_count++;
    // Op 1031: Destroy pmap 127
    if (pmaps[127]) {
        pmap_destroy(pmaps[127]);
        pmaps[127] = 0;
    }
    ops_count++;
    // Op 1032: Protect pmap 98 va=0x3b5af000
    if (pmaps[98]) {
        pmap_protect(pmaps[98], 995815424, 995819520, 1);
    }
    ops_count++;
    // Op 1033: Enter pmap 114 va=0x927f9000 pa=0xc7b2f000 prot=0x3
    if (pmaps[114]) {
        pmap_enter(pmaps[114], 2457833472, 3350392832, 3, 0);
    }
    ops_count++;
    // Op 1034: Extract pmap 125 va=0x95805000
    if (pmaps[125]) {
        pmap_extract(pmaps[125], 2508214272);
    }
    ops_count++;
    // Op 1035: Remove pmap 145 va=0x5223000
    if (pmaps[145]) {
        pmap_remove(pmaps[145], 86126592);
    }
    ops_count++;
    // Op 1036: Extract pmap 66 va=0x76e5b000
    if (pmaps[66]) {
        pmap_extract(pmaps[66], 1994764288);
    }
    ops_count++;
    // Op 1037: Remove pmap 66 va=0x76e5b000
    if (pmaps[66]) {
        pmap_remove(pmaps[66], 1994764288);
    }
    ops_count++;
    // Op 1038: Enter pmap 132 va=0xb6763000 pa=0xf79ee000 prot=0x5
    if (pmaps[132]) {
        pmap_enter(pmaps[132], 3061198848, 4154384384, 5, 0);
    }
    ops_count++;
    // Op 1039: Enter pmap 137 va=0x59451000 pa=0x7590e000 prot=0x5
    if (pmaps[137]) {
        pmap_enter(pmaps[137], 1497698304, 1972428800, 5, 0);
    }
    ops_count++;
    // Op 1040: Destroy pmap 132
    if (pmaps[132]) {
        pmap_destroy(pmaps[132]);
        pmaps[132] = 0;
    }
    ops_count++;
    // Op 1041: Remove pmap 139 va=0x95a10000
    if (pmaps[139]) {
        pmap_remove(pmaps[139], 2510356480);
    }
    ops_count++;
    // Op 1042: Create pmap 149
    pmaps[149] = pmap_create();
    if (!pmaps[149]) kprint("Warning: pmap_create failed for 149\n");
    ops_count++;
    // Op 1043: Enter pmap 137 va=0x8fbbb000 pa=0xa5c83000 prot=0x5
    if (pmaps[137]) {
        pmap_enter(pmaps[137], 2411442176, 2781360128, 5, 0);
    }
    ops_count++;
    // Op 1044: Enter pmap 100 va=0x7b534000 pa=0x1067a000 prot=0x5
    if (pmaps[100]) {
        pmap_enter(pmaps[100], 2069053440, 275226624, 5, 0);
    }
    ops_count++;
    // Op 1045: Extract pmap 134 va=0x42da9000
    if (pmaps[134]) {
        pmap_extract(pmaps[134], 1121619968);
    }
    ops_count++;
    // Op 1046: Remove pmap 133 va=0x30386000
    if (pmaps[133]) {
        pmap_remove(pmaps[133], 809000960);
    }
    ops_count++;
    // Op 1047: Create pmap 150
    pmaps[150] = pmap_create();
    if (!pmaps[150]) kprint("Warning: pmap_create failed for 150\n");
    ops_count++;
    // Op 1048: Protect pmap 94 va=0x52d6c000
    if (pmaps[94]) {
        pmap_protect(pmaps[94], 1389805568, 1389809664, 15);
    }
    ops_count++;
    // Op 1049: Remove pmap 79 va=0x92503000
    if (pmaps[79]) {
        pmap_remove(pmaps[79], 2454728704);
    }
    ops_count++;
    // Op 1050: Extract pmap 128 va=0x6793d000
    if (pmaps[128]) {
        pmap_extract(pmaps[128], 1737740288);
    }
    ops_count++;
    // Op 1051: Enter pmap 88 va=0x95411000 pa=0xb5540000 prot=0x3
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 2504069120, 3042181120, 3, 0);
    }
    ops_count++;
    // Op 1052: Enter pmap 125 va=0xafb29000 pa=0x88edd000 prot=0xf
    if (pmaps[125]) {
        pmap_enter(pmaps[125], 2947715072, 2297286656, 15, 0);
    }
    ops_count++;
    // Op 1053: Extract pmap 136 va=0x2751b000
    if (pmaps[136]) {
        pmap_extract(pmaps[136], 659664896);
    }
    ops_count++;
    // Op 1054: Destroy pmap 100
    if (pmaps[100]) {
        pmap_destroy(pmaps[100]);
        pmaps[100] = 0;
    }
    ops_count++;
    // Op 1055: Enter pmap 129 va=0xb9a5e000 pa=0xceff9000 prot=0x3
    if (pmaps[129]) {
        pmap_enter(pmaps[129], 3114655744, 3472855040, 3, 0);
    }
    ops_count++;
    // Op 1056: Extract pmap 136 va=0x2751b000
    if (pmaps[136]) {
        pmap_extract(pmaps[136], 659664896);
    }
    ops_count++;
    // Op 1057: Enter pmap 134 va=0x6ba42000 pa=0xc9d04000 prot=0x5
    if (pmaps[134]) {
        pmap_enter(pmaps[134], 1805918208, 3385868288, 5, 0);
    }
    ops_count++;
    // Op 1058: Enter pmap 133 va=0x76b91000 pa=0xd9f92000 prot=0x1
    if (pmaps[133]) {
        pmap_enter(pmaps[133], 1991839744, 3656982528, 1, 0);
    }
    ops_count++;
    // Op 1059: Enter pmap 95 va=0x78e37000 pa=0xe3c16000 prot=0x3
    if (pmaps[95]) {
        pmap_enter(pmaps[95], 2028171264, 3821101056, 3, 0);
    }
    ops_count++;
    // Op 1060: Enter pmap 139 va=0x87a38000 pa=0x4c710000 prot=0xf
    if (pmaps[139]) {
        pmap_enter(pmaps[139], 2275639296, 1282473984, 15, 0);
    }
    ops_count++;
    // Op 1061: Create pmap 151
    pmaps[151] = pmap_create();
    if (!pmaps[151]) kprint("Warning: pmap_create failed for 151\n");
    ops_count++;
    // Op 1062: Extract pmap 108 va=0x57b94000
    if (pmaps[108]) {
        pmap_extract(pmaps[108], 1471758336);
    }
    ops_count++;
    // Op 1063: Enter pmap 151 va=0x75237000 pa=0x24ba5000 prot=0x5
    if (pmaps[151]) {
        pmap_enter(pmaps[151], 1965256704, 616189952, 5, 0);
    }
    ops_count++;
    // Op 1064: Extract pmap 122 va=0x46237000
    if (pmaps[122]) {
        pmap_extract(pmaps[122], 1176727552);
    }
    ops_count++;
    // Op 1065: Enter pmap 122 va=0x503bf000 pa=0xa27e8000 prot=0x1
    if (pmaps[122]) {
        pmap_enter(pmaps[122], 1346105344, 2726199296, 1, 0);
    }
    ops_count++;
    // Op 1066: Create pmap 152
    pmaps[152] = pmap_create();
    if (!pmaps[152]) kprint("Warning: pmap_create failed for 152\n");
    ops_count++;
    // Op 1067: Enter pmap 115 va=0x93871000 pa=0xc3c53000 prot=0x3
    if (pmaps[115]) {
        pmap_enter(pmaps[115], 2475102208, 3284480000, 3, 0);
    }
    ops_count++;
    // Op 1068: Enter pmap 98 va=0x2120c000 pa=0x9d5e8000 prot=0xf
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 555794432, 2640216064, 15, 0);
    }
    ops_count++;
    // Op 1069: Remove pmap 140 va=0x2d762000
    if (pmaps[140]) {
        pmap_remove(pmaps[140], 762716160);
    }
    ops_count++;
    // Op 1070: Extract pmap 139 va=0x87a38000
    if (pmaps[139]) {
        pmap_extract(pmaps[139], 2275639296);
    }
    ops_count++;
    // Op 1071: Protect pmap 66 va=0x98d01000
    if (pmaps[66]) {
        pmap_protect(pmaps[66], 2563772416, 2563776512, 1);
    }
    ops_count++;
    // Op 1072: Create pmap 153
    pmaps[153] = pmap_create();
    if (!pmaps[153]) kprint("Warning: pmap_create failed for 153\n");
    ops_count++;
    // Op 1073: Create pmap 154
    pmaps[154] = pmap_create();
    if (!pmaps[154]) kprint("Warning: pmap_create failed for 154\n");
    ops_count++;
    // Op 1074: Protect pmap 126 va=0x91469000
    if (pmaps[126]) {
        pmap_protect(pmaps[126], 2437320704, 2437324800, 15);
    }
    ops_count++;
    // Op 1075: Create pmap 155
    pmaps[155] = pmap_create();
    if (!pmaps[155]) kprint("Warning: pmap_create failed for 155\n");
    ops_count++;
    // Op 1076: Enter pmap 152 va=0x4e5d9000 pa=0x2ad1d000 prot=0x1
    if (pmaps[152]) {
        pmap_enter(pmaps[152], 1314754560, 718393344, 1, 0);
    }
    ops_count++;
    // Op 1077: Protect pmap 134 va=0x6ba42000
    if (pmaps[134]) {
        pmap_protect(pmaps[134], 1805918208, 1805922304, 15);
    }
    ops_count++;
    // Op 1078: Enter pmap 142 va=0xafd0e000 pa=0xfee74000 prot=0x3
    if (pmaps[142]) {
        pmap_enter(pmaps[142], 2949701632, 4276568064, 3, 0);
    }
    ops_count++;
    // Op 1079: Enter pmap 146 va=0x5bf7e000 pa=0xc0179000 prot=0x3
    if (pmaps[146]) {
        pmap_enter(pmaps[146], 1542971392, 3222769664, 3, 0);
    }
    ops_count++;
    // Op 1080: Destroy pmap 140
    if (pmaps[140]) {
        pmap_destroy(pmaps[140]);
        pmaps[140] = 0;
    }
    ops_count++;
    // Op 1081: Create pmap 156
    pmaps[156] = pmap_create();
    if (!pmaps[156]) kprint("Warning: pmap_create failed for 156\n");
    ops_count++;
    // Op 1082: Destroy pmap 108
    if (pmaps[108]) {
        pmap_destroy(pmaps[108]);
        pmaps[108] = 0;
    }
    ops_count++;
    // Op 1083: Destroy pmap 79
    if (pmaps[79]) {
        pmap_destroy(pmaps[79]);
        pmaps[79] = 0;
    }
    ops_count++;
    // Op 1084: Remove pmap 123 va=0x1f879000
    if (pmaps[123]) {
        pmap_remove(pmaps[123], 528977920);
    }
    ops_count++;
    // Op 1085: Enter pmap 94 va=0x600ca000 pa=0xe0753000 prot=0x5
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 1611440128, 3765776384, 5, 0);
    }
    ops_count++;
    // Op 1086: Protect pmap 95 va=0x2b0fc000
    if (pmaps[95]) {
        pmap_protect(pmaps[95], 722452480, 722456576, 1);
    }
    ops_count++;
    // Op 1087: Remove pmap 88 va=0x97778000
    if (pmaps[88]) {
        pmap_remove(pmaps[88], 2541191168);
    }
    ops_count++;
    // Op 1088: Remove pmap 144 va=0x71bea000
    if (pmaps[144]) {
        pmap_remove(pmaps[144], 1908318208);
    }
    ops_count++;
    // Op 1089: Enter pmap 153 va=0x40b38000 pa=0x79820000 prot=0x3
    if (pmaps[153]) {
        pmap_enter(pmaps[153], 1085505536, 2038562816, 3, 0);
    }
    ops_count++;
    // Op 1090: Destroy pmap 66
    if (pmaps[66]) {
        pmap_destroy(pmaps[66]);
        pmaps[66] = 0;
    }
    ops_count++;
    // Op 1091: Enter pmap 114 va=0xa9988000 pa=0x8542f000 prot=0x5
    if (pmaps[114]) {
        pmap_enter(pmaps[114], 2845343744, 2235756544, 5, 0);
    }
    ops_count++;
    // Op 1092: Remove pmap 122 va=0x46237000
    if (pmaps[122]) {
        pmap_remove(pmaps[122], 1176727552);
    }
    ops_count++;
    // Op 1093: Protect pmap 114 va=0x5812a000
    if (pmaps[114]) {
        pmap_protect(pmaps[114], 1477615616, 1477619712, 15);
    }
    ops_count++;
    // Op 1094: Create pmap 157
    pmaps[157] = pmap_create();
    if (!pmaps[157]) kprint("Warning: pmap_create failed for 157\n");
    ops_count++;
    // Op 1095: Create pmap 158
    pmaps[158] = pmap_create();
    if (!pmaps[158]) kprint("Warning: pmap_create failed for 158\n");
    ops_count++;
    // Op 1096: Destroy pmap 128
    if (pmaps[128]) {
        pmap_destroy(pmaps[128]);
        pmaps[128] = 0;
    }
    ops_count++;
    // Op 1097: Protect pmap 153 va=0x40b38000
    if (pmaps[153]) {
        pmap_protect(pmaps[153], 1085505536, 1085509632, 15);
    }
    ops_count++;
    // Op 1098: Protect pmap 137 va=0x59451000
    if (pmaps[137]) {
        pmap_protect(pmaps[137], 1497698304, 1497702400, 1);
    }
    ops_count++;
    // Op 1099: Enter pmap 130 va=0xb02e3000 pa=0xeef13000 prot=0xf
    if (pmaps[130]) {
        pmap_enter(pmaps[130], 2955816960, 4008783872, 15, 0);
    }
    ops_count++;
    kprint(".");
    // Op 1100: Enter pmap 88 va=0x3439000 pa=0xac002000 prot=0x1
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 54759424, 2885689344, 1, 0);
    }
    ops_count++;
    // Op 1101: Extract pmap 156 va=0x262e6000
    if (pmaps[156]) {
        pmap_extract(pmaps[156], 640573440);
    }
    ops_count++;
    // Op 1102: Extract pmap 152 va=0x4e5d9000
    if (pmaps[152]) {
        pmap_extract(pmaps[152], 1314754560);
    }
    ops_count++;
    // Op 1103: Extract pmap 133 va=0x76b91000
    if (pmaps[133]) {
        pmap_extract(pmaps[133], 1991839744);
    }
    ops_count++;
    // Op 1104: Enter pmap 98 va=0x198d2000 pa=0xd03c7000 prot=0x3
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 428679168, 3493621760, 3, 0);
    }
    ops_count++;
    // Op 1105: Extract pmap 137 va=0x8fbbb000
    if (pmaps[137]) {
        pmap_extract(pmaps[137], 2411442176);
    }
    ops_count++;
    // Op 1106: Enter pmap 115 va=0x1851e000 pa=0xd9fb4000 prot=0x1
    if (pmaps[115]) {
        pmap_enter(pmaps[115], 408018944, 3657121792, 1, 0);
    }
    ops_count++;
    // Op 1107: Create pmap 159
    pmaps[159] = pmap_create();
    if (!pmaps[159]) kprint("Warning: pmap_create failed for 159\n");
    ops_count++;
    // Op 1108: Create pmap 160
    pmaps[160] = pmap_create();
    if (!pmaps[160]) kprint("Warning: pmap_create failed for 160\n");
    ops_count++;
    // Op 1109: Remove pmap 141 va=0x92702000
    if (pmaps[141]) {
        pmap_remove(pmaps[141], 2456821760);
    }
    ops_count++;
    // Op 1110: Extract pmap 159 va=0xbbaa6000
    if (pmaps[159]) {
        pmap_extract(pmaps[159], 3148505088);
    }
    ops_count++;
    // Op 1111: Create pmap 161
    pmaps[161] = pmap_create();
    if (!pmaps[161]) kprint("Warning: pmap_create failed for 161\n");
    ops_count++;
    // Op 1112: Protect pmap 126 va=0x91469000
    if (pmaps[126]) {
        pmap_protect(pmaps[126], 2437320704, 2437324800, 15);
    }
    ops_count++;
    // Op 1113: Enter pmap 149 va=0x4596e000 pa=0xd600f000 prot=0xf
    if (pmaps[149]) {
        pmap_enter(pmaps[149], 1167515648, 3590385664, 15, 0);
    }
    ops_count++;
    // Op 1114: Remove pmap 150 va=0x6bf73000
    if (pmaps[150]) {
        pmap_remove(pmaps[150], 1811361792);
    }
    ops_count++;
    // Op 1115: Remove pmap 125 va=0x95805000
    if (pmaps[125]) {
        pmap_remove(pmaps[125], 2508214272);
    }
    ops_count++;
    // Op 1116: Create pmap 162
    pmaps[162] = pmap_create();
    if (!pmaps[162]) kprint("Warning: pmap_create failed for 162\n");
    ops_count++;
    // Op 1117: Protect pmap 125 va=0xafb29000
    if (pmaps[125]) {
        pmap_protect(pmaps[125], 2947715072, 2947719168, 15);
    }
    ops_count++;
    // Op 1118: Extract pmap 150 va=0xadab3000
    if (pmaps[150]) {
        pmap_extract(pmaps[150], 2913677312);
    }
    ops_count++;
    // Op 1119: Enter pmap 159 va=0x61376000 pa=0xc7874000 prot=0x5
    if (pmaps[159]) {
        pmap_enter(pmaps[159], 1631019008, 3347529728, 5, 0);
    }
    ops_count++;
    // Op 1120: Remove pmap 122 va=0x503bf000
    if (pmaps[122]) {
        pmap_remove(pmaps[122], 1346105344);
    }
    ops_count++;
    // Op 1121: Extract pmap 115 va=0x70d0f000
    if (pmaps[115]) {
        pmap_extract(pmaps[115], 1892741120);
    }
    ops_count++;
    // Op 1122: Create pmap 163
    pmaps[163] = pmap_create();
    if (!pmaps[163]) kprint("Warning: pmap_create failed for 163\n");
    ops_count++;
    // Op 1123: Enter pmap 139 va=0x32a1000 pa=0xb46e9000 prot=0x3
    if (pmaps[139]) {
        pmap_enter(pmaps[139], 53088256, 3027144704, 3, 0);
    }
    ops_count++;
    // Op 1124: Extract pmap 162 va=0x84e3c000
    if (pmaps[162]) {
        pmap_extract(pmaps[162], 2229518336);
    }
    ops_count++;
    // Op 1125: Enter pmap 159 va=0x1861d000 pa=0x6edf1000 prot=0x3
    if (pmaps[159]) {
        pmap_enter(pmaps[159], 409063424, 1860112384, 3, 0);
    }
    ops_count++;
    // Op 1126: Enter pmap 162 va=0xad21c000 pa=0xa4431000 prot=0xf
    if (pmaps[162]) {
        pmap_enter(pmaps[162], 2904670208, 2755858432, 15, 0);
    }
    ops_count++;
    // Op 1127: Create pmap 164
    pmaps[164] = pmap_create();
    if (!pmaps[164]) kprint("Warning: pmap_create failed for 164\n");
    ops_count++;
    // Op 1128: Protect pmap 159 va=0x61376000
    if (pmaps[159]) {
        pmap_protect(pmaps[159], 1631019008, 1631023104, 1);
    }
    ops_count++;
    // Op 1129: Remove pmap 125 va=0xafb29000
    if (pmaps[125]) {
        pmap_remove(pmaps[125], 2947715072);
    }
    ops_count++;
    // Op 1130: Extract pmap 125 va=0x7ddcb000
    if (pmaps[125]) {
        pmap_extract(pmaps[125], 2111614976);
    }
    ops_count++;
    // Op 1131: Enter pmap 141 va=0x47355000 pa=0xcf3c000 prot=0x3
    if (pmaps[141]) {
        pmap_enter(pmaps[141], 1194676224, 217300992, 3, 0);
    }
    ops_count++;
    // Op 1132: Remove pmap 144 va=0x673d3000
    if (pmaps[144]) {
        pmap_remove(pmaps[144], 1732063232);
    }
    ops_count++;
    // Op 1133: Protect pmap 106 va=0x45a09000
    if (pmaps[106]) {
        pmap_protect(pmaps[106], 1168150528, 1168154624, 15);
    }
    ops_count++;
    // Op 1134: Create pmap 165
    pmaps[165] = pmap_create();
    if (!pmaps[165]) kprint("Warning: pmap_create failed for 165\n");
    ops_count++;
    // Op 1135: Enter pmap 139 va=0x1e63a000 pa=0xfb1d3000 prot=0xf
    if (pmaps[139]) {
        pmap_enter(pmaps[139], 509845504, 4212994048, 15, 0);
    }
    ops_count++;
    // Op 1136: Protect pmap 94 va=0x450f7000
    if (pmaps[94]) {
        pmap_protect(pmaps[94], 1158639616, 1158643712, 15);
    }
    ops_count++;
    // Op 1137: Enter pmap 155 va=0x8c91c000 pa=0x1b1000 prot=0x1
    if (pmaps[155]) {
        pmap_enter(pmaps[155], 2358362112, 1773568, 1, 0);
    }
    ops_count++;
    // Op 1138: Protect pmap 159 va=0x1861d000
    if (pmaps[159]) {
        pmap_protect(pmaps[159], 409063424, 409067520, 1);
    }
    ops_count++;
    // Op 1139: Remove pmap 149 va=0x4596e000
    if (pmaps[149]) {
        pmap_remove(pmaps[149], 1167515648);
    }
    ops_count++;
    // Op 1140: Enter pmap 98 va=0x39113000 pa=0xd8353000 prot=0xf
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 957427712, 3627364352, 15, 0);
    }
    ops_count++;
    // Op 1141: Enter pmap 159 va=0x99967000 pa=0x792f4000 prot=0x5
    if (pmaps[159]) {
        pmap_enter(pmaps[159], 2576773120, 2033139712, 5, 0);
    }
    ops_count++;
    // Op 1142: Protect pmap 115 va=0x93871000
    if (pmaps[115]) {
        pmap_protect(pmaps[115], 2475102208, 2475106304, 1);
    }
    ops_count++;
    // Op 1143: Create pmap 166
    pmaps[166] = pmap_create();
    if (!pmaps[166]) kprint("Warning: pmap_create failed for 166\n");
    ops_count++;
    // Op 1144: Enter pmap 160 va=0x4abf7000 pa=0x8c457000 prot=0x5
    if (pmaps[160]) {
        pmap_enter(pmaps[160], 1254060032, 2353360896, 5, 0);
    }
    ops_count++;
    // Op 1145: Protect pmap 95 va=0x2b0fc000
    if (pmaps[95]) {
        pmap_protect(pmaps[95], 722452480, 722456576, 15);
    }
    ops_count++;
    // Op 1146: Create pmap 167
    pmaps[167] = pmap_create();
    if (!pmaps[167]) kprint("Warning: pmap_create failed for 167\n");
    ops_count++;
    // Op 1147: Enter pmap 135 va=0x9c60d000 pa=0x3237e000 prot=0x3
    if (pmaps[135]) {
        pmap_enter(pmaps[135], 2623590400, 842522624, 3, 0);
    }
    ops_count++;
    // Op 1148: Create pmap 168
    pmaps[168] = pmap_create();
    if (!pmaps[168]) kprint("Warning: pmap_create failed for 168\n");
    ops_count++;
    // Op 1149: Extract pmap 130 va=0x2a953000
    if (pmaps[130]) {
        pmap_extract(pmaps[130], 714420224);
    }
    ops_count++;
    // Op 1150: Protect pmap 114 va=0xa9988000
    if (pmaps[114]) {
        pmap_protect(pmaps[114], 2845343744, 2845347840, 1);
    }
    ops_count++;
    // Op 1151: Extract pmap 163 va=0x73545000
    if (pmaps[163]) {
        pmap_extract(pmaps[163], 1934905344);
    }
    ops_count++;
    // Op 1152: Enter pmap 157 va=0x81132000 pa=0x22878000 prot=0xf
    if (pmaps[157]) {
        pmap_enter(pmaps[157], 2165514240, 579305472, 15, 0);
    }
    ops_count++;
    // Op 1153: Create pmap 169
    pmaps[169] = pmap_create();
    if (!pmaps[169]) kprint("Warning: pmap_create failed for 169\n");
    ops_count++;
    // Op 1154: Protect pmap 155 va=0x8c91c000
    if (pmaps[155]) {
        pmap_protect(pmaps[155], 2358362112, 2358366208, 15);
    }
    ops_count++;
    // Op 1155: Extract pmap 169 va=0x2d5d3000
    if (pmaps[169]) {
        pmap_extract(pmaps[169], 761081856);
    }
    ops_count++;
    // Op 1156: Remove pmap 126 va=0x91469000
    if (pmaps[126]) {
        pmap_remove(pmaps[126], 2437320704);
    }
    ops_count++;
    // Op 1157: Create pmap 170
    pmaps[170] = pmap_create();
    if (!pmaps[170]) kprint("Warning: pmap_create failed for 170\n");
    ops_count++;
    // Op 1158: Remove pmap 158 va=0x52646000
    if (pmaps[158]) {
        pmap_remove(pmaps[158], 1382309888);
    }
    ops_count++;
    // Op 1159: Enter pmap 144 va=0x6b2da000 pa=0xfad08000 prot=0x3
    if (pmaps[144]) {
        pmap_enter(pmaps[144], 1798152192, 4207968256, 3, 0);
    }
    ops_count++;
    // Op 1160: Enter pmap 141 va=0x4b4f0000 pa=0x73fdb000 prot=0x3
    if (pmaps[141]) {
        pmap_enter(pmaps[141], 1263468544, 1946005504, 3, 0);
    }
    ops_count++;
    // Op 1161: Create pmap 171
    pmaps[171] = pmap_create();
    if (!pmaps[171]) kprint("Warning: pmap_create failed for 171\n");
    ops_count++;
    // Op 1162: Remove pmap 167 va=0x8325c000
    if (pmaps[167]) {
        pmap_remove(pmaps[167], 2200289280);
    }
    ops_count++;
    // Op 1163: Enter pmap 164 va=0x8c3d1000 pa=0xdc675000 prot=0x5
    if (pmaps[164]) {
        pmap_enter(pmaps[164], 2352812032, 3697758208, 5, 0);
    }
    ops_count++;
    // Op 1164: Protect pmap 155 va=0x8c91c000
    if (pmaps[155]) {
        pmap_protect(pmaps[155], 2358362112, 2358366208, 1);
    }
    ops_count++;
    // Op 1165: Destroy pmap 165
    if (pmaps[165]) {
        pmap_destroy(pmaps[165]);
        pmaps[165] = 0;
    }
    ops_count++;
    // Op 1166: Enter pmap 142 va=0xac20b000 pa=0x2206c000 prot=0x3
    if (pmaps[142]) {
        pmap_enter(pmaps[142], 2887823360, 570867712, 3, 0);
    }
    ops_count++;
    // Op 1167: Enter pmap 164 va=0xa30a7000 pa=0x52979000 prot=0x1
    if (pmaps[164]) {
        pmap_enter(pmaps[164], 2735370240, 1385664512, 1, 0);
    }
    ops_count++;
    // Op 1168: Protect pmap 155 va=0x8c91c000
    if (pmaps[155]) {
        pmap_protect(pmaps[155], 2358362112, 2358366208, 15);
    }
    ops_count++;
    // Op 1169: Remove pmap 154 va=0x4932a000
    if (pmaps[154]) {
        pmap_remove(pmaps[154], 1228054528);
    }
    ops_count++;
    // Op 1170: Extract pmap 147 va=0x85d1b000
    if (pmaps[147]) {
        pmap_extract(pmaps[147], 2245111808);
    }
    ops_count++;
    // Op 1171: Create pmap 172
    pmaps[172] = pmap_create();
    if (!pmaps[172]) kprint("Warning: pmap_create failed for 172\n");
    ops_count++;
    // Op 1172: Enter pmap 98 va=0xab95d000 pa=0xe8da000 prot=0x3
    if (pmaps[98]) {
        pmap_enter(pmaps[98], 2878722048, 244162560, 3, 0);
    }
    ops_count++;
    // Op 1173: Enter pmap 171 va=0x29331000 pa=0xfbd8c000 prot=0x5
    if (pmaps[171]) {
        pmap_enter(pmaps[171], 691212288, 4225286144, 5, 0);
    }
    ops_count++;
    // Op 1174: Remove pmap 123 va=0xfe17000
    if (pmaps[123]) {
        pmap_remove(pmaps[123], 266432512);
    }
    ops_count++;
    // Op 1175: Remove pmap 137 va=0x59451000
    if (pmaps[137]) {
        pmap_remove(pmaps[137], 1497698304);
    }
    ops_count++;
    // Op 1176: Protect pmap 95 va=0x78e37000
    if (pmaps[95]) {
        pmap_protect(pmaps[95], 2028171264, 2028175360, 15);
    }
    ops_count++;
    // Op 1177: Create pmap 173
    pmaps[173] = pmap_create();
    if (!pmaps[173]) kprint("Warning: pmap_create failed for 173\n");
    ops_count++;
    // Op 1178: Extract pmap 168 va=0x396c4000
    if (pmaps[168]) {
        pmap_extract(pmaps[168], 963395584);
    }
    ops_count++;
    // Op 1179: Protect pmap 155 va=0x8c91c000
    if (pmaps[155]) {
        pmap_protect(pmaps[155], 2358362112, 2358366208, 1);
    }
    ops_count++;
    // Op 1180: Enter pmap 171 va=0xb2358000 pa=0x49b2a000 prot=0x1
    if (pmaps[171]) {
        pmap_enter(pmaps[171], 2989850624, 1236443136, 1, 0);
    }
    ops_count++;
    // Op 1181: Extract pmap 144 va=0x6b2da000
    if (pmaps[144]) {
        pmap_extract(pmaps[144], 1798152192);
    }
    ops_count++;
    // Op 1182: Extract pmap 98 va=0x2120c000
    if (pmaps[98]) {
        pmap_extract(pmaps[98], 555794432);
    }
    ops_count++;
    // Op 1183: Destroy pmap 170
    if (pmaps[170]) {
        pmap_destroy(pmaps[170]);
        pmaps[170] = 0;
    }
    ops_count++;
    // Op 1184: Extract pmap 148 va=0xa2400000
    if (pmaps[148]) {
        pmap_extract(pmaps[148], 2722103296);
    }
    ops_count++;
    // Op 1185: Enter pmap 157 va=0x8bef0000 pa=0xbd1b0000 prot=0x3
    if (pmaps[157]) {
        pmap_enter(pmaps[157], 2347696128, 3172663296, 3, 0);
    }
    ops_count++;
    // Op 1186: Protect pmap 129 va=0xb9a5e000
    if (pmaps[129]) {
        pmap_protect(pmaps[129], 3114655744, 3114659840, 15);
    }
    ops_count++;
    // Op 1187: Destroy pmap 163
    if (pmaps[163]) {
        pmap_destroy(pmaps[163]);
        pmaps[163] = 0;
    }
    ops_count++;
    // Op 1188: Create pmap 174
    pmaps[174] = pmap_create();
    if (!pmaps[174]) kprint("Warning: pmap_create failed for 174\n");
    ops_count++;
    // Op 1189: Extract pmap 169 va=0x60902000
    if (pmaps[169]) {
        pmap_extract(pmaps[169], 1620058112);
    }
    ops_count++;
    // Op 1190: Create pmap 175
    pmaps[175] = pmap_create();
    if (!pmaps[175]) kprint("Warning: pmap_create failed for 175\n");
    ops_count++;
    // Op 1191: Enter pmap 135 va=0x214c1000 pa=0x2942a000 prot=0x5
    if (pmaps[135]) {
        pmap_enter(pmaps[135], 558632960, 692232192, 5, 0);
    }
    ops_count++;
    // Op 1192: Create pmap 176
    pmaps[176] = pmap_create();
    if (!pmaps[176]) kprint("Warning: pmap_create failed for 176\n");
    ops_count++;
    // Op 1193: Enter pmap 123 va=0xa399000 pa=0x27691000 prot=0x5
    if (pmaps[123]) {
        pmap_enter(pmaps[123], 171544576, 661196800, 5, 0);
    }
    ops_count++;
    // Op 1194: Remove pmap 172 va=0xa0181000
    if (pmaps[172]) {
        pmap_remove(pmaps[172], 2685931520);
    }
    ops_count++;
    // Op 1195: Remove pmap 162 va=0xad21c000
    if (pmaps[162]) {
        pmap_remove(pmaps[162], 2904670208);
    }
    ops_count++;
    // Op 1196: Enter pmap 156 va=0x8cd75000 pa=0x5bbb7000 prot=0x1
    if (pmaps[156]) {
        pmap_enter(pmaps[156], 2362920960, 1539010560, 1, 0);
    }
    ops_count++;
    // Op 1197: Protect pmap 139 va=0x1e63a000
    if (pmaps[139]) {
        pmap_protect(pmaps[139], 509845504, 509849600, 1);
    }
    ops_count++;
    // Op 1198: Extract pmap 153 va=0x40b38000
    if (pmaps[153]) {
        pmap_extract(pmaps[153], 1085505536);
    }
    ops_count++;
    // Op 1199: Enter pmap 154 va=0x67466000 pa=0x1799b000 prot=0x5
    if (pmaps[154]) {
        pmap_enter(pmaps[154], 1732665344, 395948032, 5, 0);
    }
    ops_count++;
    kprint(".");
    // Op 1200: Destroy pmap 130
    if (pmaps[130]) {
        pmap_destroy(pmaps[130]);
        pmaps[130] = 0;
    }
    ops_count++;
    // Op 1201: Remove pmap 142 va=0xac20b000
    if (pmaps[142]) {
        pmap_remove(pmaps[142], 2887823360);
    }
    ops_count++;
    // Op 1202: Enter pmap 172 va=0x9e47b000 pa=0x87dec000 prot=0xf
    if (pmaps[172]) {
        pmap_enter(pmaps[172], 2655498240, 2279522304, 15, 0);
    }
    ops_count++;
    // Op 1203: Extract pmap 176 va=0xa6389000
    if (pmaps[176]) {
        pmap_extract(pmaps[176], 2788724736);
    }
    ops_count++;
    // Op 1204: Create pmap 177
    pmaps[177] = pmap_create();
    if (!pmaps[177]) kprint("Warning: pmap_create failed for 177\n");
    ops_count++;
    // Op 1205: Remove pmap 149 va=0x3d878000
    if (pmaps[149]) {
        pmap_remove(pmaps[149], 1032290304);
    }
    ops_count++;
    // Op 1206: Extract pmap 160 va=0x3e6b8000
    if (pmaps[160]) {
        pmap_extract(pmaps[160], 1047232512);
    }
    ops_count++;
    // Op 1207: Extract pmap 166 va=0x61a1b000
    if (pmaps[166]) {
        pmap_extract(pmaps[166], 1637986304);
    }
    ops_count++;
    // Op 1208: Extract pmap 115 va=0x1851e000
    if (pmaps[115]) {
        pmap_extract(pmaps[115], 408018944);
    }
    ops_count++;
    // Op 1209: Create pmap 178
    pmaps[178] = pmap_create();
    if (!pmaps[178]) kprint("Warning: pmap_create failed for 178\n");
    ops_count++;
    // Op 1210: Enter pmap 173 va=0x5ffd5000 pa=0x83b9c000 prot=0xf
    if (pmaps[173]) {
        pmap_enter(pmaps[173], 1610436608, 2209988608, 15, 0);
    }
    ops_count++;
    // Op 1211: Enter pmap 138 va=0xa6ca9000 pa=0x31587000 prot=0x5
    if (pmaps[138]) {
        pmap_enter(pmaps[138], 2798292992, 827879424, 5, 0);
    }
    ops_count++;
    // Op 1212: Enter pmap 114 va=0x2ada7000 pa=0xf8cca000 prot=0x1
    if (pmaps[114]) {
        pmap_enter(pmaps[114], 718958592, 4174159872, 1, 0);
    }
    ops_count++;
    // Op 1213: Remove pmap 133 va=0x76b91000
    if (pmaps[133]) {
        pmap_remove(pmaps[133], 1991839744);
    }
    ops_count++;
    // Op 1214: Extract pmap 148 va=0x34a0e000
    if (pmaps[148]) {
        pmap_extract(pmaps[148], 882958336);
    }
    ops_count++;
    // Op 1215: Enter pmap 167 va=0xbdcad000 pa=0xc683f000 prot=0xf
    if (pmaps[167]) {
        pmap_enter(pmaps[167], 3184185344, 3330535424, 15, 0);
    }
    ops_count++;
    // Op 1216: Remove pmap 105 va=0xae618000
    if (pmaps[105]) {
        pmap_remove(pmaps[105], 2925625344);
    }
    ops_count++;
    // Op 1217: Remove pmap 94 va=0xbb7b7000
    if (pmaps[94]) {
        pmap_remove(pmaps[94], 3145428992);
    }
    ops_count++;
    // Op 1218: Protect pmap 95 va=0x2b0fc000
    if (pmaps[95]) {
        pmap_protect(pmaps[95], 722452480, 722456576, 1);
    }
    ops_count++;
    // Op 1219: Create pmap 179
    pmaps[179] = pmap_create();
    if (!pmaps[179]) kprint("Warning: pmap_create failed for 179\n");
    ops_count++;
    // Op 1220: Protect pmap 156 va=0x8cd75000
    if (pmaps[156]) {
        pmap_protect(pmaps[156], 2362920960, 2362925056, 1);
    }
    ops_count++;
    // Op 1221: Extract pmap 147 va=0xa155f000
    if (pmaps[147]) {
        pmap_extract(pmaps[147], 2706763776);
    }
    ops_count++;
    // Op 1222: Enter pmap 141 va=0x4530000 pa=0x63c66000 prot=0x1
    if (pmaps[141]) {
        pmap_enter(pmaps[141], 72548352, 1673945088, 1, 0);
    }
    ops_count++;
    // Op 1223: Enter pmap 162 va=0xb864b000 pa=0x9e5c2000 prot=0x1
    if (pmaps[162]) {
        pmap_enter(pmaps[162], 3093606400, 2656837632, 1, 0);
    }
    ops_count++;
    // Op 1224: Create pmap 180
    pmaps[180] = pmap_create();
    if (!pmaps[180]) kprint("Warning: pmap_create failed for 180\n");
    ops_count++;
    // Op 1225: Extract pmap 114 va=0x927f9000
    if (pmaps[114]) {
        pmap_extract(pmaps[114], 2457833472);
    }
    ops_count++;
    // Op 1226: Remove pmap 138 va=0xa6ca9000
    if (pmaps[138]) {
        pmap_remove(pmaps[138], 2798292992);
    }
    ops_count++;
    // Op 1227: Enter pmap 176 va=0xa9253000 pa=0x7f3c000 prot=0xf
    if (pmaps[176]) {
        pmap_enter(pmaps[176], 2837786624, 133414912, 15, 0);
    }
    ops_count++;
    // Op 1228: Destroy pmap 134
    if (pmaps[134]) {
        pmap_destroy(pmaps[134]);
        pmaps[134] = 0;
    }
    ops_count++;
    // Op 1229: Create pmap 181
    pmaps[181] = pmap_create();
    if (!pmaps[181]) kprint("Warning: pmap_create failed for 181\n");
    ops_count++;
    // Op 1230: Enter pmap 176 va=0x8bb4a000 pa=0x4b93b000 prot=0x3
    if (pmaps[176]) {
        pmap_enter(pmaps[176], 2343870464, 1267970048, 3, 0);
    }
    ops_count++;
    // Op 1231: Enter pmap 181 va=0xbab51000 pa=0xa5ef6000 prot=0x5
    if (pmaps[181]) {
        pmap_enter(pmaps[181], 3132428288, 2783928320, 5, 0);
    }
    ops_count++;
    // Op 1232: Extract pmap 88 va=0x95411000
    if (pmaps[88]) {
        pmap_extract(pmaps[88], 2504069120);
    }
    ops_count++;
    // Op 1233: Create pmap 182
    pmaps[182] = pmap_create();
    if (!pmaps[182]) kprint("Warning: pmap_create failed for 182\n");
    ops_count++;
    // Op 1234: Create pmap 183
    pmaps[183] = pmap_create();
    if (!pmaps[183]) kprint("Warning: pmap_create failed for 183\n");
    ops_count++;
    // Op 1235: Remove pmap 147 va=0x8982d000
    if (pmaps[147]) {
        pmap_remove(pmaps[147], 2307051520);
    }
    ops_count++;
    // Op 1236: Enter pmap 106 va=0x4e004000 pa=0x35d60000 prot=0x5
    if (pmaps[106]) {
        pmap_enter(pmaps[106], 1308639232, 903217152, 5, 0);
    }
    ops_count++;
    // Op 1237: Destroy pmap 182
    if (pmaps[182]) {
        pmap_destroy(pmaps[182]);
        pmaps[182] = 0;
    }
    ops_count++;
    // Op 1238: Remove pmap 154 va=0x67466000
    if (pmaps[154]) {
        pmap_remove(pmaps[154], 1732665344);
    }
    ops_count++;
    // Op 1239: Destroy pmap 135
    if (pmaps[135]) {
        pmap_destroy(pmaps[135]);
        pmaps[135] = 0;
    }
    ops_count++;
    // Op 1240: Remove pmap 138 va=0x31bf6000
    if (pmaps[138]) {
        pmap_remove(pmaps[138], 834625536);
    }
    ops_count++;
    // Op 1241: Create pmap 184
    pmaps[184] = pmap_create();
    if (!pmaps[184]) kprint("Warning: pmap_create failed for 184\n");
    ops_count++;
    // Op 1242: Remove pmap 159 va=0x61376000
    if (pmaps[159]) {
        pmap_remove(pmaps[159], 1631019008);
    }
    ops_count++;
    // Op 1243: Extract pmap 158 va=0x8cb6e000
    if (pmaps[158]) {
        pmap_extract(pmaps[158], 2360795136);
    }
    ops_count++;
    // Op 1244: Create pmap 185
    pmaps[185] = pmap_create();
    if (!pmaps[185]) kprint("Warning: pmap_create failed for 185\n");
    ops_count++;
    // Op 1245: Remove pmap 138 va=0x1923d000
    if (pmaps[138]) {
        pmap_remove(pmaps[138], 421777408);
    }
    ops_count++;
    // Op 1246: Destroy pmap 160
    if (pmaps[160]) {
        pmap_destroy(pmaps[160]);
        pmaps[160] = 0;
    }
    ops_count++;
    // Op 1247: Enter pmap 144 va=0x5b6d7000 pa=0x20e2b000 prot=0x3
    if (pmaps[144]) {
        pmap_enter(pmaps[144], 1533898752, 551727104, 3, 0);
    }
    ops_count++;
    // Op 1248: Create pmap 186
    pmaps[186] = pmap_create();
    if (!pmaps[186]) kprint("Warning: pmap_create failed for 186\n");
    ops_count++;
    // Op 1249: Remove pmap 185 va=0x31892000
    if (pmaps[185]) {
        pmap_remove(pmaps[185], 831070208);
    }
    ops_count++;
    // Op 1250: Enter pmap 151 va=0x540e8000 pa=0x9dd89000 prot=0x5
    if (pmaps[151]) {
        pmap_enter(pmaps[151], 1410236416, 2648215552, 5, 0);
    }
    ops_count++;
    // Op 1251: Remove pmap 123 va=0xa399000
    if (pmaps[123]) {
        pmap_remove(pmaps[123], 171544576);
    }
    ops_count++;
    // Op 1252: Destroy pmap 143
    if (pmaps[143]) {
        pmap_destroy(pmaps[143]);
        pmaps[143] = 0;
    }
    ops_count++;
    // Op 1253: Destroy pmap 136
    if (pmaps[136]) {
        pmap_destroy(pmaps[136]);
        pmaps[136] = 0;
    }
    ops_count++;
    // Op 1254: Enter pmap 183 va=0x5c075000 pa=0x2271d000 prot=0x5
    if (pmaps[183]) {
        pmap_enter(pmaps[183], 1543983104, 577884160, 5, 0);
    }
    ops_count++;
    // Op 1255: Extract pmap 154 va=0x4e337000
    if (pmaps[154]) {
        pmap_extract(pmaps[154], 1311993856);
    }
    ops_count++;
    // Op 1256: Enter pmap 114 va=0x2cff5000 pa=0xa552d000 prot=0x1
    if (pmaps[114]) {
        pmap_enter(pmaps[114], 754929664, 2773667840, 1, 0);
    }
    ops_count++;
    // Op 1257: Protect pmap 159 va=0x1861d000
    if (pmaps[159]) {
        pmap_protect(pmaps[159], 409063424, 409067520, 15);
    }
    ops_count++;
    // Op 1258: Create pmap 187
    pmaps[187] = pmap_create();
    if (!pmaps[187]) kprint("Warning: pmap_create failed for 187\n");
    ops_count++;
    // Op 1259: Create pmap 188
    pmaps[188] = pmap_create();
    if (!pmaps[188]) kprint("Warning: pmap_create failed for 188\n");
    ops_count++;
    // Op 1260: Create pmap 189
    pmaps[189] = pmap_create();
    if (!pmaps[189]) kprint("Warning: pmap_create failed for 189\n");
    ops_count++;
    // Op 1261: Enter pmap 94 va=0x94f6d000 pa=0x9442d000 prot=0xf
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 2499203072, 2487406592, 15, 0);
    }
    ops_count++;
    // Op 1262: Extract pmap 161 va=0x8a285000
    if (pmaps[161]) {
        pmap_extract(pmaps[161], 2317897728);
    }
    ops_count++;
    // Op 1263: Enter pmap 178 va=0x260cd000 pa=0x5db64000 prot=0xf
    if (pmaps[178]) {
        pmap_enter(pmaps[178], 638373888, 1572225024, 15, 0);
    }
    ops_count++;
    // Op 1264: Destroy pmap 178
    if (pmaps[178]) {
        pmap_destroy(pmaps[178]);
        pmaps[178] = 0;
    }
    ops_count++;
    // Op 1265: Remove pmap 185 va=0x4a7ef000
    if (pmaps[185]) {
        pmap_remove(pmaps[185], 1249832960);
    }
    ops_count++;
    // Op 1266: Create pmap 190
    pmaps[190] = pmap_create();
    if (!pmaps[190]) kprint("Warning: pmap_create failed for 190\n");
    ops_count++;
    // Op 1267: Enter pmap 106 va=0x6593c000 pa=0x5d364000 prot=0x1
    if (pmaps[106]) {
        pmap_enter(pmaps[106], 1704181760, 1563836416, 1, 0);
    }
    ops_count++;
    // Op 1268: Remove pmap 185 va=0x9d318000
    if (pmaps[185]) {
        pmap_remove(pmaps[185], 2637266944);
    }
    ops_count++;
    // Op 1269: Remove pmap 177 va=0x839e8000
    if (pmaps[177]) {
        pmap_remove(pmaps[177], 2208202752);
    }
    ops_count++;
    // Op 1270: Create pmap 191
    pmaps[191] = pmap_create();
    if (!pmaps[191]) kprint("Warning: pmap_create failed for 191\n");
    ops_count++;
    // Op 1271: Destroy pmap 162
    if (pmaps[162]) {
        pmap_destroy(pmaps[162]);
        pmaps[162] = 0;
    }
    ops_count++;
    // Op 1272: Extract pmap 172 va=0x256d5000
    if (pmaps[172]) {
        pmap_extract(pmaps[172], 627920896);
    }
    ops_count++;
    // Op 1273: Protect pmap 155 va=0x8c91c000
    if (pmaps[155]) {
        pmap_protect(pmaps[155], 2358362112, 2358366208, 1);
    }
    ops_count++;
    // Op 1274: Destroy pmap 156
    if (pmaps[156]) {
        pmap_destroy(pmaps[156]);
        pmaps[156] = 0;
    }
    ops_count++;
    // Op 1275: Destroy pmap 139
    if (pmaps[139]) {
        pmap_destroy(pmaps[139]);
        pmaps[139] = 0;
    }
    ops_count++;
    // Op 1276: Enter pmap 95 va=0xab287000 pa=0x34ff2000 prot=0x5
    if (pmaps[95]) {
        pmap_enter(pmaps[95], 2871554048, 889135104, 5, 0);
    }
    ops_count++;
    // Op 1277: Enter pmap 155 va=0xbe36000 pa=0xa473000 prot=0x5
    if (pmaps[155]) {
        pmap_enter(pmaps[155], 199450624, 172437504, 5, 0);
    }
    ops_count++;
    // Op 1278: Create pmap 192
    pmaps[192] = pmap_create();
    if (!pmaps[192]) kprint("Warning: pmap_create failed for 192\n");
    ops_count++;
    // Op 1279: Extract pmap 172 va=0xbdedd000
    if (pmaps[172]) {
        pmap_extract(pmaps[172], 3186479104);
    }
    ops_count++;
    // Op 1280: Enter pmap 164 va=0x91f61000 pa=0x2c916000 prot=0xf
    if (pmaps[164]) {
        pmap_enter(pmaps[164], 2448822272, 747724800, 15, 0);
    }
    ops_count++;
    // Op 1281: Remove pmap 149 va=0x96722000
    if (pmaps[149]) {
        pmap_remove(pmaps[149], 2524061696);
    }
    ops_count++;
    // Op 1282: Remove pmap 187 va=0x13ed3000
    if (pmaps[187]) {
        pmap_remove(pmaps[187], 334311424);
    }
    ops_count++;
    // Op 1283: Enter pmap 150 va=0x390ff000 pa=0x8440f000 prot=0xf
    if (pmaps[150]) {
        pmap_enter(pmaps[150], 957345792, 2218848256, 15, 0);
    }
    ops_count++;
    // Op 1284: Enter pmap 189 va=0x58541000 pa=0x7cb2f000 prot=0xf
    if (pmaps[189]) {
        pmap_enter(pmaps[189], 1481904128, 2092101632, 15, 0);
    }
    ops_count++;
    // Op 1285: Extract pmap 88 va=0x64977000
    if (pmaps[88]) {
        pmap_extract(pmaps[88], 1687646208);
    }
    ops_count++;
    // Op 1286: Enter pmap 137 va=0x13bc1000 pa=0x83c8f000 prot=0x5
    if (pmaps[137]) {
        pmap_enter(pmaps[137], 331091968, 2210983936, 5, 0);
    }
    ops_count++;
    // Op 1287: Destroy pmap 126
    if (pmaps[126]) {
        pmap_destroy(pmaps[126]);
        pmaps[126] = 0;
    }
    ops_count++;
    // Op 1288: Destroy pmap 171
    if (pmaps[171]) {
        pmap_destroy(pmaps[171]);
        pmaps[171] = 0;
    }
    ops_count++;
    // Op 1289: Protect pmap 115 va=0x93871000
    if (pmaps[115]) {
        pmap_protect(pmaps[115], 2475102208, 2475106304, 15);
    }
    ops_count++;
    // Op 1290: Protect pmap 181 va=0xbab51000
    if (pmaps[181]) {
        pmap_protect(pmaps[181], 3132428288, 3132432384, 15);
    }
    ops_count++;
    // Op 1291: Remove pmap 158 va=0x35f0c000
    if (pmaps[158]) {
        pmap_remove(pmaps[158], 904970240);
    }
    ops_count++;
    // Op 1292: Remove pmap 175 va=0x6930c000
    if (pmaps[175]) {
        pmap_remove(pmaps[175], 1764802560);
    }
    ops_count++;
    // Op 1293: Create pmap 193
    pmaps[193] = pmap_create();
    if (!pmaps[193]) kprint("Warning: pmap_create failed for 193\n");
    ops_count++;
    // Op 1294: Create pmap 194
    pmaps[194] = pmap_create();
    if (!pmaps[194]) kprint("Warning: pmap_create failed for 194\n");
    ops_count++;
    // Op 1295: Remove pmap 125 va=0x227d5000
    if (pmaps[125]) {
        pmap_remove(pmaps[125], 578637824);
    }
    ops_count++;
    // Op 1296: Remove pmap 95 va=0xab287000
    if (pmaps[95]) {
        pmap_remove(pmaps[95], 2871554048);
    }
    ops_count++;
    // Op 1297: Create pmap 195
    pmaps[195] = pmap_create();
    if (!pmaps[195]) kprint("Warning: pmap_create failed for 195\n");
    ops_count++;
    // Op 1298: Destroy pmap 195
    if (pmaps[195]) {
        pmap_destroy(pmaps[195]);
        pmaps[195] = 0;
    }
    ops_count++;
    // Op 1299: Destroy pmap 152
    if (pmaps[152]) {
        pmap_destroy(pmaps[152]);
        pmaps[152] = 0;
    }
    ops_count++;
    kprint(".");
    // Op 1300: Remove pmap 169 va=0xa33b4000
    if (pmaps[169]) {
        pmap_remove(pmaps[169], 2738569216);
    }
    ops_count++;
    // Op 1301: Create pmap 196
    pmaps[196] = pmap_create();
    if (!pmaps[196]) kprint("Warning: pmap_create failed for 196\n");
    ops_count++;
    // Op 1302: Protect pmap 106 va=0x4e004000
    if (pmaps[106]) {
        pmap_protect(pmaps[106], 1308639232, 1308643328, 15);
    }
    ops_count++;
    // Op 1303: Remove pmap 159 va=0x99967000
    if (pmaps[159]) {
        pmap_remove(pmaps[159], 2576773120);
    }
    ops_count++;
    // Op 1304: Destroy pmap 190
    if (pmaps[190]) {
        pmap_destroy(pmaps[190]);
        pmaps[190] = 0;
    }
    ops_count++;
    // Op 1305: Enter pmap 123 va=0x95285000 pa=0xd2193000 prot=0x3
    if (pmaps[123]) {
        pmap_enter(pmaps[123], 2502447104, 3524866048, 3, 0);
    }
    ops_count++;
    // Op 1306: Remove pmap 164 va=0xa30a7000
    if (pmaps[164]) {
        pmap_remove(pmaps[164], 2735370240);
    }
    ops_count++;
    // Op 1307: Destroy pmap 179
    if (pmaps[179]) {
        pmap_destroy(pmaps[179]);
        pmaps[179] = 0;
    }
    ops_count++;
    // Op 1308: Enter pmap 158 va=0x98b67000 pa=0x97414000 prot=0x1
    if (pmaps[158]) {
        pmap_enter(pmaps[158], 2562093056, 2537635840, 1, 0);
    }
    ops_count++;
    // Op 1309: Destroy pmap 151
    if (pmaps[151]) {
        pmap_destroy(pmaps[151]);
        pmaps[151] = 0;
    }
    ops_count++;
    // Op 1310: Create pmap 197
    pmaps[197] = pmap_create();
    if (!pmaps[197]) kprint("Warning: pmap_create failed for 197\n");
    ops_count++;
    // Op 1311: Create pmap 198
    pmaps[198] = pmap_create();
    if (!pmaps[198]) kprint("Warning: pmap_create failed for 198\n");
    ops_count++;
    // Op 1312: Enter pmap 166 va=0xaf32c000 pa=0x6b87a000 prot=0x3
    if (pmaps[166]) {
        pmap_enter(pmaps[166], 2939338752, 1804050432, 3, 0);
    }
    ops_count++;
    // Op 1313: Remove pmap 129 va=0xb9a5e000
    if (pmaps[129]) {
        pmap_remove(pmaps[129], 3114655744);
    }
    ops_count++;
    // Op 1314: Extract pmap 174 va=0x880bd000
    if (pmaps[174]) {
        pmap_extract(pmaps[174], 2282475520);
    }
    ops_count++;
    // Op 1315: Enter pmap 115 va=0x17db5000 pa=0x7b842000 prot=0x3
    if (pmaps[115]) {
        pmap_enter(pmaps[115], 400248832, 2072256512, 3, 0);
    }
    ops_count++;
    // Op 1316: Extract pmap 168 va=0xadb62000
    if (pmaps[168]) {
        pmap_extract(pmaps[168], 2914394112);
    }
    ops_count++;
    // Op 1317: Remove pmap 167 va=0xbdcad000
    if (pmaps[167]) {
        pmap_remove(pmaps[167], 3184185344);
    }
    ops_count++;
    // Op 1318: Enter pmap 95 va=0xb298c000 pa=0x87b20000 prot=0xf
    if (pmaps[95]) {
        pmap_enter(pmaps[95], 2996355072, 2276589568, 15, 0);
    }
    ops_count++;
    // Op 1319: Destroy pmap 191
    if (pmaps[191]) {
        pmap_destroy(pmaps[191]);
        pmaps[191] = 0;
    }
    ops_count++;
    // Op 1320: Enter pmap 188 va=0x6198c000 pa=0x59cb8000 prot=0x1
    if (pmaps[188]) {
        pmap_enter(pmaps[188], 1637400576, 1506508800, 1, 0);
    }
    ops_count++;
    // Op 1321: Enter pmap 142 va=0x7a25f000 pa=0xc41f5000 prot=0x3
    if (pmaps[142]) {
        pmap_enter(pmaps[142], 2049306624, 3290386432, 3, 0);
    }
    ops_count++;
    // Op 1322: Extract pmap 106 va=0x6593c000
    if (pmaps[106]) {
        pmap_extract(pmaps[106], 1704181760);
    }
    ops_count++;
    // Op 1323: Enter pmap 133 va=0x1668000 pa=0xb59ac000 prot=0x1
    if (pmaps[133]) {
        pmap_enter(pmaps[133], 23494656, 3046817792, 1, 0);
    }
    ops_count++;
    // Op 1324: Remove pmap 115 va=0x17db5000
    if (pmaps[115]) {
        pmap_remove(pmaps[115], 400248832);
    }
    ops_count++;
    // Op 1325: Create pmap 199
    pmaps[199] = pmap_create();
    if (!pmaps[199]) kprint("Warning: pmap_create failed for 199\n");
    ops_count++;
    // Op 1326: Remove pmap 183 va=0x5c075000
    if (pmaps[183]) {
        pmap_remove(pmaps[183], 1543983104);
    }
    ops_count++;
    // Op 1327: Enter pmap 114 va=0x279fa000 pa=0x6d230000 prot=0x5
    if (pmaps[114]) {
        pmap_enter(pmaps[114], 664772608, 1831010304, 5, 0);
    }
    ops_count++;
    // Op 1328: Enter pmap 168 va=0x6c6e9000 pa=0x909e3000 prot=0x5
    if (pmaps[168]) {
        pmap_enter(pmaps[168], 1819185152, 2426286080, 5, 0);
    }
    ops_count++;
    // Op 1329: Remove pmap 181 va=0xbab51000
    if (pmaps[181]) {
        pmap_remove(pmaps[181], 3132428288);
    }
    ops_count++;
    // Op 1330: Protect pmap 155 va=0xbe36000
    if (pmaps[155]) {
        pmap_protect(pmaps[155], 199450624, 199454720, 1);
    }
    ops_count++;
    // Op 1331: Enter pmap 169 va=0x7bd7c000 pa=0x41079000 prot=0x5
    if (pmaps[169]) {
        pmap_enter(pmaps[169], 2077736960, 1091014656, 5, 0);
    }
    ops_count++;
    // Op 1332: Remove pmap 188 va=0x6198c000
    if (pmaps[188]) {
        pmap_remove(pmaps[188], 1637400576);
    }
    ops_count++;
    // Op 1333: Extract pmap 115 va=0x1851e000
    if (pmaps[115]) {
        pmap_extract(pmaps[115], 408018944);
    }
    ops_count++;
    // Op 1334: Create pmap 200
    pmaps[200] = pmap_create();
    if (!pmaps[200]) kprint("Warning: pmap_create failed for 200\n");
    ops_count++;
    // Op 1335: Enter pmap 189 va=0x56a3e000 pa=0xcd14c000 prot=0x1
    if (pmaps[189]) {
        pmap_enter(pmaps[189], 1453580288, 3440689152, 1, 0);
    }
    ops_count++;
    // Op 1336: Destroy pmap 176
    if (pmaps[176]) {
        pmap_destroy(pmaps[176]);
        pmaps[176] = 0;
    }
    ops_count++;
    // Op 1337: Create pmap 201
    pmaps[201] = pmap_create();
    if (!pmaps[201]) kprint("Warning: pmap_create failed for 201\n");
    ops_count++;
    // Op 1338: Create pmap 202
    pmaps[202] = pmap_create();
    if (!pmaps[202]) kprint("Warning: pmap_create failed for 202\n");
    ops_count++;
    // Op 1339: Create pmap 203
    pmaps[203] = pmap_create();
    if (!pmaps[203]) kprint("Warning: pmap_create failed for 203\n");
    ops_count++;
    // Op 1340: Extract pmap 157 va=0x8bef0000
    if (pmaps[157]) {
        pmap_extract(pmaps[157], 2347696128);
    }
    ops_count++;
    // Op 1341: Destroy pmap 106
    if (pmaps[106]) {
        pmap_destroy(pmaps[106]);
        pmaps[106] = 0;
    }
    ops_count++;
    // Op 1342: Remove pmap 187 va=0x62634000
    if (pmaps[187]) {
        pmap_remove(pmaps[187], 1650671616);
    }
    ops_count++;
    // Op 1343: Extract pmap 155 va=0xbe36000
    if (pmaps[155]) {
        pmap_extract(pmaps[155], 199450624);
    }
    ops_count++;
    // Op 1344: Create pmap 204
    pmaps[204] = pmap_create();
    if (!pmaps[204]) kprint("Warning: pmap_create failed for 204\n");
    ops_count++;
    // Op 1345: Protect pmap 133 va=0x1668000
    if (pmaps[133]) {
        pmap_protect(pmaps[133], 23494656, 23498752, 15);
    }
    ops_count++;
    // Op 1346: Create pmap 205
    pmaps[205] = pmap_create();
    if (!pmaps[205]) kprint("Warning: pmap_create failed for 205\n");
    ops_count++;
    // Op 1347: Extract pmap 203 va=0x4937c000
    if (pmaps[203]) {
        pmap_extract(pmaps[203], 1228390400);
    }
    ops_count++;
    // Op 1348: Destroy pmap 193
    if (pmaps[193]) {
        pmap_destroy(pmaps[193]);
        pmaps[193] = 0;
    }
    ops_count++;
    // Op 1349: Remove pmap 168 va=0x6c6e9000
    if (pmaps[168]) {
        pmap_remove(pmaps[168], 1819185152);
    }
    ops_count++;
    // Op 1350: Remove pmap 184 va=0x93b5000
    if (pmaps[184]) {
        pmap_remove(pmaps[184], 154882048);
    }
    ops_count++;
    // Op 1351: Enter pmap 181 va=0x12e000 pa=0xf9d3e000 prot=0x3
    if (pmaps[181]) {
        pmap_enter(pmaps[181], 1236992, 4191412224, 3, 0);
    }
    ops_count++;
    // Op 1352: Create pmap 206
    pmaps[206] = pmap_create();
    if (!pmaps[206]) kprint("Warning: pmap_create failed for 206\n");
    ops_count++;
    // Op 1353: Enter pmap 155 va=0x5a66a000 pa=0xab7ef000 prot=0xf
    if (pmaps[155]) {
        pmap_enter(pmaps[155], 1516675072, 2877222912, 15, 0);
    }
    ops_count++;
    // Op 1354: Enter pmap 137 va=0x848ad000 pa=0x3ff0000 prot=0x1
    if (pmaps[137]) {
        pmap_enter(pmaps[137], 2223689728, 67043328, 1, 0);
    }
    ops_count++;
    // Op 1355: Extract pmap 172 va=0x9e47b000
    if (pmaps[172]) {
        pmap_extract(pmaps[172], 2655498240);
    }
    ops_count++;
    // Op 1356: Enter pmap 197 va=0x46c94000 pa=0x39fe8000 prot=0x3
    if (pmaps[197]) {
        pmap_enter(pmaps[197], 1187594240, 972980224, 3, 0);
    }
    ops_count++;
    // Op 1357: Enter pmap 155 va=0x932d2000 pa=0x140d000 prot=0x5
    if (pmaps[155]) {
        pmap_enter(pmaps[155], 2469208064, 21024768, 5, 0);
    }
    ops_count++;
    // Op 1358: Destroy pmap 167
    if (pmaps[167]) {
        pmap_destroy(pmaps[167]);
        pmaps[167] = 0;
    }
    ops_count++;
    // Op 1359: Create pmap 207
    pmaps[207] = pmap_create();
    if (!pmaps[207]) kprint("Warning: pmap_create failed for 207\n");
    ops_count++;
    // Op 1360: Create pmap 208
    pmaps[208] = pmap_create();
    if (!pmaps[208]) kprint("Warning: pmap_create failed for 208\n");
    ops_count++;
    // Op 1361: Enter pmap 206 va=0x1df45000 pa=0xcd9d9000 prot=0x5
    if (pmaps[206]) {
        pmap_enter(pmaps[206], 502550528, 3449655296, 5, 0);
    }
    ops_count++;
    // Op 1362: Extract pmap 198 va=0x40095000
    if (pmaps[198]) {
        pmap_extract(pmaps[198], 1074352128);
    }
    ops_count++;
    // Op 1363: Remove pmap 207 va=0x60886000
    if (pmaps[207]) {
        pmap_remove(pmaps[207], 1619550208);
    }
    ops_count++;
    // Op 1364: Remove pmap 194 va=0x3e473000
    if (pmaps[194]) {
        pmap_remove(pmaps[194], 1044852736);
    }
    ops_count++;
    // Op 1365: Enter pmap 175 va=0x7008000 pa=0x7b726000 prot=0x1
    if (pmaps[175]) {
        pmap_enter(pmaps[175], 117473280, 2071093248, 1, 0);
    }
    ops_count++;
    // Op 1366: Enter pmap 154 va=0xa237b000 pa=0xc9f55000 prot=0x3
    if (pmaps[154]) {
        pmap_enter(pmaps[154], 2721558528, 3388297216, 3, 0);
    }
    ops_count++;
    // Op 1367: Remove pmap 141 va=0x4b4f0000
    if (pmaps[141]) {
        pmap_remove(pmaps[141], 1263468544);
    }
    ops_count++;
    // Op 1368: Enter pmap 150 va=0x8439a000 pa=0x89307000 prot=0x1
    if (pmaps[150]) {
        pmap_enter(pmaps[150], 2218369024, 2301652992, 1, 0);
    }
    ops_count++;
    // Op 1369: Protect pmap 144 va=0x6b2da000
    if (pmaps[144]) {
        pmap_protect(pmaps[144], 1798152192, 1798156288, 15);
    }
    ops_count++;
    // Op 1370: Create pmap 209
    pmaps[209] = pmap_create();
    if (!pmaps[209]) kprint("Warning: pmap_create failed for 209\n");
    ops_count++;
    // Op 1371: Remove pmap 185 va=0x1376a000
    if (pmaps[185]) {
        pmap_remove(pmaps[185], 326541312);
    }
    ops_count++;
    // Op 1372: Remove pmap 150 va=0x8439a000
    if (pmaps[150]) {
        pmap_remove(pmaps[150], 2218369024);
    }
    ops_count++;
    // Op 1373: Enter pmap 158 va=0x92dee000 pa=0x44f22000 prot=0x3
    if (pmaps[158]) {
        pmap_enter(pmaps[158], 2464079872, 1156718592, 3, 0);
    }
    ops_count++;
    // Op 1374: Extract pmap 154 va=0xa237b000
    if (pmaps[154]) {
        pmap_extract(pmaps[154], 2721558528);
    }
    ops_count++;
    // Op 1375: Destroy pmap 164
    if (pmaps[164]) {
        pmap_destroy(pmaps[164]);
        pmaps[164] = 0;
    }
    ops_count++;
    // Op 1376: Extract pmap 138 va=0x3fec1000
    if (pmaps[138]) {
        pmap_extract(pmaps[138], 1072435200);
    }
    ops_count++;
    // Op 1377: Protect pmap 150 va=0x390ff000
    if (pmaps[150]) {
        pmap_protect(pmaps[150], 957345792, 957349888, 1);
    }
    ops_count++;
    // Op 1378: Create pmap 210
    pmaps[210] = pmap_create();
    if (!pmaps[210]) kprint("Warning: pmap_create failed for 210\n");
    ops_count++;
    // Op 1379: Create pmap 211
    pmaps[211] = pmap_create();
    if (!pmaps[211]) kprint("Warning: pmap_create failed for 211\n");
    ops_count++;
    // Op 1380: Extract pmap 210 va=0x8a9e1000
    if (pmaps[210]) {
        pmap_extract(pmaps[210], 2325614592);
    }
    ops_count++;
    // Op 1381: Extract pmap 208 va=0x50bf3000
    if (pmaps[208]) {
        pmap_extract(pmaps[208], 1354706944);
    }
    ops_count++;
    // Op 1382: Enter pmap 174 va=0x7da57000 pa=0x2ee0b000 prot=0x5
    if (pmaps[174]) {
        pmap_enter(pmaps[174], 2107994112, 786477056, 5, 0);
    }
    ops_count++;
    // Op 1383: Enter pmap 144 va=0xddcb000 pa=0x86a69000 prot=0xf
    if (pmaps[144]) {
        pmap_enter(pmaps[144], 232566784, 2259062784, 15, 0);
    }
    ops_count++;
    // Op 1384: Enter pmap 105 va=0x80a24000 pa=0x58f7e000 prot=0xf
    if (pmaps[105]) {
        pmap_enter(pmaps[105], 2158116864, 1492639744, 15, 0);
    }
    ops_count++;
    // Op 1385: Extract pmap 150 va=0x390ff000
    if (pmaps[150]) {
        pmap_extract(pmaps[150], 957345792);
    }
    ops_count++;
    // Op 1386: Remove pmap 205 va=0x50ce9000
    if (pmaps[205]) {
        pmap_remove(pmaps[205], 1355714560);
    }
    ops_count++;
    // Op 1387: Remove pmap 94 va=0xa0c9e000
    if (pmaps[94]) {
        pmap_remove(pmaps[94], 2697584640);
    }
    ops_count++;
    // Op 1388: Enter pmap 137 va=0x534fd000 pa=0x5ca1f000 prot=0x5
    if (pmaps[137]) {
        pmap_enter(pmaps[137], 1397739520, 1554116608, 5, 0);
    }
    ops_count++;
    // Op 1389: Destroy pmap 172
    if (pmaps[172]) {
        pmap_destroy(pmaps[172]);
        pmaps[172] = 0;
    }
    ops_count++;
    // Op 1390: Destroy pmap 208
    if (pmaps[208]) {
        pmap_destroy(pmaps[208]);
        pmaps[208] = 0;
    }
    ops_count++;
    // Op 1391: Extract pmap 149 va=0x7faf7000
    if (pmaps[149]) {
        pmap_extract(pmaps[149], 2142203904);
    }
    ops_count++;
    // Op 1392: Enter pmap 157 va=0xb361f000 pa=0xc09fe000 prot=0x5
    if (pmaps[157]) {
        pmap_enter(pmaps[157], 3009540096, 3231703040, 5, 0);
    }
    ops_count++;
    // Op 1393: Enter pmap 155 va=0x4795d000 pa=0x7372f000 prot=0x3
    if (pmaps[155]) {
        pmap_enter(pmaps[155], 1201000448, 1936912384, 3, 0);
    }
    ops_count++;
    // Op 1394: Remove pmap 211 va=0x936dc000
    if (pmaps[211]) {
        pmap_remove(pmaps[211], 2473443328);
    }
    ops_count++;
    // Op 1395: Remove pmap 200 va=0x19912000
    if (pmaps[200]) {
        pmap_remove(pmaps[200], 428941312);
    }
    ops_count++;
    // Op 1396: Destroy pmap 158
    if (pmaps[158]) {
        pmap_destroy(pmaps[158]);
        pmaps[158] = 0;
    }
    ops_count++;
    // Op 1397: Extract pmap 137 va=0x8fbbb000
    if (pmaps[137]) {
        pmap_extract(pmaps[137], 2411442176);
    }
    ops_count++;
    // Op 1398: Enter pmap 88 va=0x2d363000 pa=0x6bf8c000 prot=0xf
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 758525952, 1811464192, 15, 0);
    }
    ops_count++;
    // Op 1399: Create pmap 212
    pmaps[212] = pmap_create();
    if (!pmaps[212]) kprint("Warning: pmap_create failed for 212\n");
    ops_count++;
    kprint(".");
    // Op 1400: Extract pmap 185 va=0xb876b000
    if (pmaps[185]) {
        pmap_extract(pmaps[185], 3094786048);
    }
    ops_count++;
    // Op 1401: Enter pmap 169 va=0x4ccf1000 pa=0x404fd000 prot=0x1
    if (pmaps[169]) {
        pmap_enter(pmaps[169], 1288638464, 1078972416, 1, 0);
    }
    ops_count++;
    // Op 1402: Enter pmap 189 va=0x29d0d000 pa=0xf6e8000 prot=0x3
    if (pmaps[189]) {
        pmap_enter(pmaps[189], 701550592, 258899968, 3, 0);
    }
    ops_count++;
    // Op 1403: Protect pmap 115 va=0x1851e000
    if (pmaps[115]) {
        pmap_protect(pmaps[115], 408018944, 408023040, 15);
    }
    ops_count++;
    // Op 1404: Enter pmap 153 va=0x8f732000 pa=0x8ec7c000 prot=0x5
    if (pmaps[153]) {
        pmap_enter(pmaps[153], 2406686720, 2395455488, 5, 0);
    }
    ops_count++;
    // Op 1405: Enter pmap 157 va=0x1f097000 pa=0x59f11000 prot=0x3
    if (pmaps[157]) {
        pmap_enter(pmaps[157], 520712192, 1508970496, 3, 0);
    }
    ops_count++;
    // Op 1406: Remove pmap 186 va=0xa62d8000
    if (pmaps[186]) {
        pmap_remove(pmaps[186], 2787999744);
    }
    ops_count++;
    // Op 1407: Enter pmap 212 va=0x48dc2000 pa=0x5af86000 prot=0x5
    if (pmaps[212]) {
        pmap_enter(pmaps[212], 1222385664, 1526226944, 5, 0);
    }
    ops_count++;
    // Op 1408: Remove pmap 169 va=0x4ccf1000
    if (pmaps[169]) {
        pmap_remove(pmaps[169], 1288638464);
    }
    ops_count++;
    // Op 1409: Extract pmap 154 va=0xa237b000
    if (pmaps[154]) {
        pmap_extract(pmaps[154], 2721558528);
    }
    ops_count++;
    // Op 1410: Remove pmap 125 va=0x2ae64000
    if (pmaps[125]) {
        pmap_remove(pmaps[125], 719732736);
    }
    ops_count++;
    // Op 1411: Remove pmap 209 va=0x565f2000
    if (pmaps[209]) {
        pmap_remove(pmaps[209], 1449074688);
    }
    ops_count++;
    // Op 1412: Enter pmap 184 va=0x9256e000 pa=0x67af2000 prot=0x1
    if (pmaps[184]) {
        pmap_enter(pmaps[184], 2455166976, 1739530240, 1, 0);
    }
    ops_count++;
    // Op 1413: Create pmap 213
    pmaps[213] = pmap_create();
    if (!pmaps[213]) kprint("Warning: pmap_create failed for 213\n");
    ops_count++;
    // Op 1414: Enter pmap 185 va=0x511ef000 pa=0xa73bd000 prot=0x5
    if (pmaps[185]) {
        pmap_enter(pmaps[185], 1360982016, 2805714944, 5, 0);
    }
    ops_count++;
    // Op 1415: Enter pmap 205 va=0x5cf86000 pa=0xbf861000 prot=0xf
    if (pmaps[205]) {
        pmap_enter(pmaps[205], 1559781376, 3213234176, 15, 0);
    }
    ops_count++;
    // Op 1416: Enter pmap 153 va=0x5ea0000 pa=0x20129000 prot=0x3
    if (pmaps[153]) {
        pmap_enter(pmaps[153], 99221504, 538087424, 3, 0);
    }
    ops_count++;
    // Op 1417: Remove pmap 129 va=0x35e81000
    if (pmaps[129]) {
        pmap_remove(pmaps[129], 904400896);
    }
    ops_count++;
    // Op 1418: Destroy pmap 138
    if (pmaps[138]) {
        pmap_destroy(pmaps[138]);
        pmaps[138] = 0;
    }
    ops_count++;
    // Op 1419: Create pmap 214
    pmaps[214] = pmap_create();
    if (!pmaps[214]) kprint("Warning: pmap_create failed for 214\n");
    ops_count++;
    // Op 1420: Extract pmap 211 va=0x6bfe000
    if (pmaps[211]) {
        pmap_extract(pmaps[211], 113238016);
    }
    ops_count++;
    // Op 1421: Create pmap 215
    pmaps[215] = pmap_create();
    if (!pmaps[215]) kprint("Warning: pmap_create failed for 215\n");
    ops_count++;
    // Op 1422: Remove pmap 129 va=0xa7a32000
    if (pmaps[129]) {
        pmap_remove(pmaps[129], 2812485632);
    }
    ops_count++;
    // Op 1423: Extract pmap 199 va=0x3f557000
    if (pmaps[199]) {
        pmap_extract(pmaps[199], 1062563840);
    }
    ops_count++;
    // Op 1424: Protect pmap 206 va=0x1df45000
    if (pmaps[206]) {
        pmap_protect(pmaps[206], 502550528, 502554624, 1);
    }
    ops_count++;
    // Op 1425: Enter pmap 150 va=0x7508000 pa=0x90fe000 prot=0x5
    if (pmaps[150]) {
        pmap_enter(pmaps[150], 122716160, 152035328, 5, 0);
    }
    ops_count++;
    // Op 1426: Extract pmap 187 va=0x779a0000
    if (pmaps[187]) {
        pmap_extract(pmaps[187], 2006581248);
    }
    ops_count++;
    // Op 1427: Extract pmap 177 va=0x2296f000
    if (pmaps[177]) {
        pmap_extract(pmaps[177], 580317184);
    }
    ops_count++;
    // Op 1428: Extract pmap 153 va=0x8f732000
    if (pmaps[153]) {
        pmap_extract(pmaps[153], 2406686720);
    }
    ops_count++;
    // Op 1429: Remove pmap 205 va=0x5cf86000
    if (pmaps[205]) {
        pmap_remove(pmaps[205], 1559781376);
    }
    ops_count++;
    // Op 1430: Extract pmap 212 va=0x48dc2000
    if (pmaps[212]) {
        pmap_extract(pmaps[212], 1222385664);
    }
    ops_count++;
    // Op 1431: Enter pmap 196 va=0x69864000 pa=0x4292b000 prot=0x3
    if (pmaps[196]) {
        pmap_enter(pmaps[196], 1770405888, 1116909568, 3, 0);
    }
    ops_count++;
    // Op 1432: Enter pmap 88 va=0x85f81000 pa=0xd0ccb000 prot=0x3
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 2247626752, 3503075328, 3, 0);
    }
    ops_count++;
    // Op 1433: Enter pmap 211 va=0x88fc4000 pa=0x2311000 prot=0xf
    if (pmaps[211]) {
        pmap_enter(pmaps[211], 2298232832, 36769792, 15, 0);
    }
    ops_count++;
    // Op 1434: Extract pmap 212 va=0x48dc2000
    if (pmaps[212]) {
        pmap_extract(pmaps[212], 1222385664);
    }
    ops_count++;
    // Op 1435: Protect pmap 197 va=0x46c94000
    if (pmaps[197]) {
        pmap_protect(pmaps[197], 1187594240, 1187598336, 15);
    }
    ops_count++;
    // Op 1436: Extract pmap 205 va=0x29cc1000
    if (pmaps[205]) {
        pmap_extract(pmaps[205], 701239296);
    }
    ops_count++;
    // Op 1437: Protect pmap 166 va=0xaf32c000
    if (pmaps[166]) {
        pmap_protect(pmaps[166], 2939338752, 2939342848, 1);
    }
    ops_count++;
    // Op 1438: Enter pmap 169 va=0x84409000 pa=0xc3c8c000 prot=0x5
    if (pmaps[169]) {
        pmap_enter(pmaps[169], 2218823680, 3284713472, 5, 0);
    }
    ops_count++;
    // Op 1439: Enter pmap 177 va=0x69b42000 pa=0x3030c000 prot=0x1
    if (pmaps[177]) {
        pmap_enter(pmaps[177], 1773412352, 808501248, 1, 0);
    }
    ops_count++;
    // Op 1440: Destroy pmap 183
    if (pmaps[183]) {
        pmap_destroy(pmaps[183]);
        pmaps[183] = 0;
    }
    ops_count++;
    // Op 1441: Create pmap 216
    pmaps[216] = pmap_create();
    if (!pmaps[216]) kprint("Warning: pmap_create failed for 216\n");
    ops_count++;
    // Op 1442: Enter pmap 209 va=0x45aaa000 pa=0xb3622000 prot=0xf
    if (pmaps[209]) {
        pmap_enter(pmaps[209], 1168809984, 3009552384, 15, 0);
    }
    ops_count++;
    // Op 1443: Destroy pmap 95
    if (pmaps[95]) {
        pmap_destroy(pmaps[95]);
        pmaps[95] = 0;
    }
    ops_count++;
    // Op 1444: Destroy pmap 98
    if (pmaps[98]) {
        pmap_destroy(pmaps[98]);
        pmaps[98] = 0;
    }
    ops_count++;
    // Op 1445: Destroy pmap 123
    if (pmaps[123]) {
        pmap_destroy(pmaps[123]);
        pmaps[123] = 0;
    }
    ops_count++;
    // Op 1446: Enter pmap 133 va=0xb7172000 pa=0x704c1000 prot=0x1
    if (pmaps[133]) {
        pmap_enter(pmaps[133], 3071746048, 1884033024, 1, 0);
    }
    ops_count++;
    // Op 1447: Destroy pmap 137
    if (pmaps[137]) {
        pmap_destroy(pmaps[137]);
        pmaps[137] = 0;
    }
    ops_count++;
    // Op 1448: Create pmap 217
    pmaps[217] = pmap_create();
    if (!pmaps[217]) kprint("Warning: pmap_create failed for 217\n");
    ops_count++;
    // Op 1449: Destroy pmap 169
    if (pmaps[169]) {
        pmap_destroy(pmaps[169]);
        pmaps[169] = 0;
    }
    ops_count++;
    // Op 1450: Enter pmap 207 va=0x7f96e000 pa=0xff8a8000 prot=0x5
    if (pmaps[207]) {
        pmap_enter(pmaps[207], 2140594176, 4287266816, 5, 0);
    }
    ops_count++;
    // Op 1451: Enter pmap 206 va=0x81ff000 pa=0x549b2000 prot=0x1
    if (pmaps[206]) {
        pmap_enter(pmaps[206], 136310784, 1419452416, 1, 0);
    }
    ops_count++;
    // Op 1452: Create pmap 218
    pmaps[218] = pmap_create();
    if (!pmaps[218]) kprint("Warning: pmap_create failed for 218\n");
    ops_count++;
    // Op 1453: Enter pmap 168 va=0x12465000 pa=0x8b1ea000 prot=0xf
    if (pmaps[168]) {
        pmap_enter(pmaps[168], 306597888, 2334040064, 15, 0);
    }
    ops_count++;
    // Op 1454: Destroy pmap 214
    if (pmaps[214]) {
        pmap_destroy(pmaps[214]);
        pmaps[214] = 0;
    }
    ops_count++;
    // Op 1455: Protect pmap 209 va=0x45aaa000
    if (pmaps[209]) {
        pmap_protect(pmaps[209], 1168809984, 1168814080, 1);
    }
    ops_count++;
    // Op 1456: Create pmap 219
    pmaps[219] = pmap_create();
    if (!pmaps[219]) kprint("Warning: pmap_create failed for 219\n");
    ops_count++;
    // Op 1457: Enter pmap 175 va=0x3f461000 pa=0xbce49000 prot=0x1
    if (pmaps[175]) {
        pmap_enter(pmaps[175], 1061556224, 3169095680, 1, 0);
    }
    ops_count++;
    // Op 1458: Destroy pmap 203
    if (pmaps[203]) {
        pmap_destroy(pmaps[203]);
        pmaps[203] = 0;
    }
    ops_count++;
    // Op 1459: Protect pmap 185 va=0x511ef000
    if (pmaps[185]) {
        pmap_protect(pmaps[185], 1360982016, 1360986112, 15);
    }
    ops_count++;
    // Op 1460: Enter pmap 154 va=0x52b45000 pa=0x28aac000 prot=0x1
    if (pmaps[154]) {
        pmap_enter(pmaps[154], 1387548672, 682278912, 1, 0);
    }
    ops_count++;
    // Op 1461: Enter pmap 149 va=0x7b31e000 pa=0xa3860000 prot=0x5
    if (pmaps[149]) {
        pmap_enter(pmaps[149], 2066866176, 2743468032, 5, 0);
    }
    ops_count++;
    // Op 1462: Destroy pmap 215
    if (pmaps[215]) {
        pmap_destroy(pmaps[215]);
        pmaps[215] = 0;
    }
    ops_count++;
    // Op 1463: Create pmap 220
    pmaps[220] = pmap_create();
    if (!pmaps[220]) kprint("Warning: pmap_create failed for 220\n");
    ops_count++;
    // Op 1464: Remove pmap 129 va=0x3c685000
    if (pmaps[129]) {
        pmap_remove(pmaps[129], 1013469184);
    }
    ops_count++;
    // Op 1465: Remove pmap 155 va=0x8c91c000
    if (pmaps[155]) {
        pmap_remove(pmaps[155], 2358362112);
    }
    ops_count++;
    // Op 1466: Destroy pmap 150
    if (pmaps[150]) {
        pmap_destroy(pmaps[150]);
        pmaps[150] = 0;
    }
    ops_count++;
    // Op 1467: Remove pmap 173 va=0x5ffd5000
    if (pmaps[173]) {
        pmap_remove(pmaps[173], 1610436608);
    }
    ops_count++;
    // Op 1468: Create pmap 221
    pmaps[221] = pmap_create();
    if (!pmaps[221]) kprint("Warning: pmap_create failed for 221\n");
    ops_count++;
    // Op 1469: Create pmap 222
    pmaps[222] = pmap_create();
    if (!pmaps[222]) kprint("Warning: pmap_create failed for 222\n");
    ops_count++;
    // Op 1470: Protect pmap 181 va=0x12e000
    if (pmaps[181]) {
        pmap_protect(pmaps[181], 1236992, 1241088, 15);
    }
    ops_count++;
    // Op 1471: Destroy pmap 122
    if (pmaps[122]) {
        pmap_destroy(pmaps[122]);
        pmaps[122] = 0;
    }
    ops_count++;
    // Op 1472: Remove pmap 161 va=0xa008b000
    if (pmaps[161]) {
        pmap_remove(pmaps[161], 2684923904);
    }
    ops_count++;
    // Op 1473: Create pmap 223
    pmaps[223] = pmap_create();
    if (!pmaps[223]) kprint("Warning: pmap_create failed for 223\n");
    ops_count++;
    // Op 1474: Enter pmap 142 va=0xb38a4000 pa=0x895e7000 prot=0x1
    if (pmaps[142]) {
        pmap_enter(pmaps[142], 3012182016, 2304667648, 1, 0);
    }
    ops_count++;
    // Op 1475: Create pmap 224
    pmaps[224] = pmap_create();
    if (!pmaps[224]) kprint("Warning: pmap_create failed for 224\n");
    ops_count++;
    // Op 1476: Destroy pmap 224
    if (pmaps[224]) {
        pmap_destroy(pmaps[224]);
        pmaps[224] = 0;
    }
    ops_count++;
    // Op 1477: Remove pmap 199 va=0x58e9e000
    if (pmaps[199]) {
        pmap_remove(pmaps[199], 1491722240);
    }
    ops_count++;
    // Op 1478: Protect pmap 94 va=0x94f6d000
    if (pmaps[94]) {
        pmap_protect(pmaps[94], 2499203072, 2499207168, 15);
    }
    ops_count++;
    // Op 1479: Protect pmap 153 va=0x40b38000
    if (pmaps[153]) {
        pmap_protect(pmaps[153], 1085505536, 1085509632, 15);
    }
    ops_count++;
    // Op 1480: Protect pmap 189 va=0x56a3e000
    if (pmaps[189]) {
        pmap_protect(pmaps[189], 1453580288, 1453584384, 15);
    }
    ops_count++;
    // Op 1481: Destroy pmap 145
    if (pmaps[145]) {
        pmap_destroy(pmaps[145]);
        pmaps[145] = 0;
    }
    ops_count++;
    // Op 1482: Enter pmap 161 va=0xbb7ec000 pa=0x77f8b000 prot=0x5
    if (pmaps[161]) {
        pmap_enter(pmaps[161], 3145646080, 2012786688, 5, 0);
    }
    ops_count++;
    // Op 1483: Extract pmap 197 va=0x8df51000
    if (pmaps[197]) {
        pmap_extract(pmaps[197], 2381647872);
    }
    ops_count++;
    // Op 1484: Destroy pmap 188
    if (pmaps[188]) {
        pmap_destroy(pmaps[188]);
        pmaps[188] = 0;
    }
    ops_count++;
    // Op 1485: Destroy pmap 157
    if (pmaps[157]) {
        pmap_destroy(pmaps[157]);
        pmaps[157] = 0;
    }
    ops_count++;
    // Op 1486: Enter pmap 88 va=0xa355b000 pa=0x81967000 prot=0xf
    if (pmaps[88]) {
        pmap_enter(pmaps[88], 2740301824, 2174119936, 15, 0);
    }
    ops_count++;
    // Op 1487: Create pmap 225
    pmaps[225] = pmap_create();
    if (!pmaps[225]) kprint("Warning: pmap_create failed for 225\n");
    ops_count++;
    // Op 1488: Extract pmap 212 va=0x48dc2000
    if (pmaps[212]) {
        pmap_extract(pmaps[212], 1222385664);
    }
    ops_count++;
    // Op 1489: Extract pmap 217 va=0x86297000
    if (pmaps[217]) {
        pmap_extract(pmaps[217], 2250862592);
    }
    ops_count++;
    // Op 1490: Remove pmap 212 va=0x48dc2000
    if (pmaps[212]) {
        pmap_remove(pmaps[212], 1222385664);
    }
    ops_count++;
    // Op 1491: Remove pmap 166 va=0xaf32c000
    if (pmaps[166]) {
        pmap_remove(pmaps[166], 2939338752);
    }
    ops_count++;
    // Op 1492: Remove pmap 133 va=0x1668000
    if (pmaps[133]) {
        pmap_remove(pmaps[133], 23494656);
    }
    ops_count++;
    // Op 1493: Enter pmap 115 va=0xb350000 pa=0xd5df3000 prot=0x3
    if (pmaps[115]) {
        pmap_enter(pmaps[115], 188022784, 3588173824, 3, 0);
    }
    ops_count++;
    // Op 1494: Destroy pmap 105
    if (pmaps[105]) {
        pmap_destroy(pmaps[105]);
        pmaps[105] = 0;
    }
    ops_count++;
    // Op 1495: Destroy pmap 189
    if (pmaps[189]) {
        pmap_destroy(pmaps[189]);
        pmaps[189] = 0;
    }
    ops_count++;
    // Op 1496: Destroy pmap 114
    if (pmaps[114]) {
        pmap_destroy(pmaps[114]);
        pmaps[114] = 0;
    }
    ops_count++;
    // Op 1497: Remove pmap 221 va=0x637b4000
    if (pmaps[221]) {
        pmap_remove(pmaps[221], 1669021696);
    }
    ops_count++;
    // Op 1498: Remove pmap 197 va=0x46c94000
    if (pmaps[197]) {
        pmap_remove(pmaps[197], 1187594240);
    }
    ops_count++;
    // Op 1499: Create pmap 226
    pmaps[226] = pmap_create();
    if (!pmaps[226]) kprint("Warning: pmap_create failed for 226\n");
    ops_count++;
    kprint(".");
    // Op 1500: Destroy pmap 211
    if (pmaps[211]) {
        pmap_destroy(pmaps[211]);
        pmaps[211] = 0;
    }
    ops_count++;
    // Op 1501: Create pmap 227
    pmaps[227] = pmap_create();
    if (!pmaps[227]) kprint("Warning: pmap_create failed for 227\n");
    ops_count++;
    // Op 1502: Extract pmap 153 va=0x40b38000
    if (pmaps[153]) {
        pmap_extract(pmaps[153], 1085505536);
    }
    ops_count++;
    // Op 1503: Enter pmap 205 va=0xf8a1000 pa=0x5c75a000 prot=0xf
    if (pmaps[205]) {
        pmap_enter(pmaps[205], 260706304, 1551212544, 15, 0);
    }
    ops_count++;
    // Op 1504: Remove pmap 197 va=0xa684000
    if (pmaps[197]) {
        pmap_remove(pmaps[197], 174604288);
    }
    ops_count++;
    // Op 1505: Enter pmap 153 va=0x84fd0000 pa=0x18ecc000 prot=0x1
    if (pmaps[153]) {
        pmap_enter(pmaps[153], 2231173120, 418168832, 1, 0);
    }
    ops_count++;
    // Op 1506: Enter pmap 149 va=0x86b49000 pa=0xa8390000 prot=0x3
    if (pmaps[149]) {
        pmap_enter(pmaps[149], 2259980288, 2822307840, 3, 0);
    }
    ops_count++;
    // Op 1507: Remove pmap 148 va=0x8ef4e000
    if (pmaps[148]) {
        pmap_remove(pmaps[148], 2398412800);
    }
    ops_count++;
    // Op 1508: Protect pmap 184 va=0x9256e000
    if (pmaps[184]) {
        pmap_protect(pmaps[184], 2455166976, 2455171072, 15);
    }
    ops_count++;
    // Op 1509: Remove pmap 210 va=0xaa4d6000
    if (pmaps[210]) {
        pmap_remove(pmaps[210], 2857197568);
    }
    ops_count++;
    // Op 1510: Enter pmap 141 va=0x2ab41000 pa=0xcaeec000 prot=0x3
    if (pmaps[141]) {
        pmap_enter(pmaps[141], 716443648, 3404644352, 3, 0);
    }
    ops_count++;
    // Op 1511: Enter pmap 196 va=0x56674000 pa=0xa8640000 prot=0x3
    if (pmaps[196]) {
        pmap_enter(pmaps[196], 1449607168, 2825125888, 3, 0);
    }
    ops_count++;
    // Op 1512: Enter pmap 202 va=0x71694000 pa=0xef0b2000 prot=0x5
    if (pmaps[202]) {
        pmap_enter(pmaps[202], 1902723072, 4010483712, 5, 0);
    }
    ops_count++;
    // Op 1513: Extract pmap 199 va=0x531c9000
    if (pmaps[199]) {
        pmap_extract(pmaps[199], 1394380800);
    }
    ops_count++;
    // Op 1514: Protect pmap 184 va=0x9256e000
    if (pmaps[184]) {
        pmap_protect(pmaps[184], 2455166976, 2455171072, 15);
    }
    ops_count++;
    // Op 1515: Remove pmap 200 va=0x8bae3000
    if (pmaps[200]) {
        pmap_remove(pmaps[200], 2343448576);
    }
    ops_count++;
    // Op 1516: Destroy pmap 218
    if (pmaps[218]) {
        pmap_destroy(pmaps[218]);
        pmaps[218] = 0;
    }
    ops_count++;
    // Op 1517: Remove pmap 216 va=0x410c0000
    if (pmaps[216]) {
        pmap_remove(pmaps[216], 1091305472);
    }
    ops_count++;
    // Op 1518: Enter pmap 186 va=0x728f6000 pa=0xf5c70000 prot=0xf
    if (pmaps[186]) {
        pmap_enter(pmaps[186], 1921998848, 4123459584, 15, 0);
    }
    ops_count++;
    // Op 1519: Create pmap 228
    pmaps[228] = pmap_create();
    if (!pmaps[228]) kprint("Warning: pmap_create failed for 228\n");
    ops_count++;
    // Op 1520: Create pmap 229
    pmaps[229] = pmap_create();
    if (!pmaps[229]) kprint("Warning: pmap_create failed for 229\n");
    ops_count++;
    // Op 1521: Enter pmap 94 va=0x84821000 pa=0x22a4f000 prot=0x5
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 2223116288, 581234688, 5, 0);
    }
    ops_count++;
    // Op 1522: Create pmap 230
    pmaps[230] = pmap_create();
    if (!pmaps[230]) kprint("Warning: pmap_create failed for 230\n");
    ops_count++;
    // Op 1523: Extract pmap 153 va=0x58e4000
    if (pmaps[153]) {
        pmap_extract(pmaps[153], 93208576);
    }
    ops_count++;
    // Op 1524: Remove pmap 199 va=0x92175000
    if (pmaps[199]) {
        pmap_remove(pmaps[199], 2451001344);
    }
    ops_count++;
    // Op 1525: Extract pmap 146 va=0x5bf7e000
    if (pmaps[146]) {
        pmap_extract(pmaps[146], 1542971392);
    }
    ops_count++;
    // Op 1526: Enter pmap 144 va=0x43d65000 pa=0x5e092000 prot=0x3
    if (pmaps[144]) {
        pmap_enter(pmaps[144], 1138118656, 1577656320, 3, 0);
    }
    ops_count++;
    // Op 1527: Destroy pmap 221
    if (pmaps[221]) {
        pmap_destroy(pmaps[221]);
        pmaps[221] = 0;
    }
    ops_count++;
    // Op 1528: Enter pmap 200 va=0x66593000 pa=0x91be3000 prot=0x3
    if (pmaps[200]) {
        pmap_enter(pmaps[200], 1717121024, 2445160448, 3, 0);
    }
    ops_count++;
    // Op 1529: Enter pmap 192 va=0x2c436000 pa=0x7d0fa000 prot=0x1
    if (pmaps[192]) {
        pmap_enter(pmaps[192], 742612992, 2098176000, 1, 0);
    }
    ops_count++;
    // Op 1530: Remove pmap 168 va=0x12465000
    if (pmaps[168]) {
        pmap_remove(pmaps[168], 306597888);
    }
    ops_count++;
    // Op 1531: Protect pmap 200 va=0x66593000
    if (pmaps[200]) {
        pmap_protect(pmaps[200], 1717121024, 1717125120, 1);
    }
    ops_count++;
    // Op 1532: Protect pmap 175 va=0x3f461000
    if (pmaps[175]) {
        pmap_protect(pmaps[175], 1061556224, 1061560320, 15);
    }
    ops_count++;
    // Op 1533: Extract pmap 142 va=0x3a0a3000
    if (pmaps[142]) {
        pmap_extract(pmaps[142], 973746176);
    }
    ops_count++;
    // Op 1534: Remove pmap 88 va=0x85f81000
    if (pmaps[88]) {
        pmap_remove(pmaps[88], 2247626752);
    }
    ops_count++;
    // Op 1535: Create pmap 231
    pmaps[231] = pmap_create();
    if (!pmaps[231]) kprint("Warning: pmap_create failed for 231\n");
    ops_count++;
    // Op 1536: Enter pmap 173 va=0x8a38e000 pa=0x90ae2000 prot=0x3
    if (pmaps[173]) {
        pmap_enter(pmaps[173], 2318983168, 2427330560, 3, 0);
    }
    ops_count++;
    // Op 1537: Enter pmap 210 va=0x38863000 pa=0x2747f000 prot=0x1
    if (pmaps[210]) {
        pmap_enter(pmaps[210], 948318208, 659025920, 1, 0);
    }
    ops_count++;
    // Op 1538: Remove pmap 181 va=0x12e000
    if (pmaps[181]) {
        pmap_remove(pmaps[181], 1236992);
    }
    ops_count++;
    // Op 1539: Protect pmap 200 va=0x66593000
    if (pmaps[200]) {
        pmap_protect(pmaps[200], 1717121024, 1717125120, 1);
    }
    ops_count++;
    // Op 1540: Enter pmap 220 va=0x64f26000 pa=0x25f42000 prot=0x3
    if (pmaps[220]) {
        pmap_enter(pmaps[220], 1693605888, 636755968, 3, 0);
    }
    ops_count++;
    // Op 1541: Enter pmap 192 va=0x9249f000 pa=0xbedcf000 prot=0x1
    if (pmaps[192]) {
        pmap_enter(pmaps[192], 2454319104, 3202150400, 1, 0);
    }
    ops_count++;
    // Op 1542: Enter pmap 159 va=0x15c7c000 pa=0x6615d000 prot=0xf
    if (pmaps[159]) {
        pmap_enter(pmaps[159], 365412352, 1712705536, 15, 0);
    }
    ops_count++;
    // Op 1543: Protect pmap 144 va=0x5b6d7000
    if (pmaps[144]) {
        pmap_protect(pmaps[144], 1533898752, 1533902848, 15);
    }
    ops_count++;
    // Op 1544: Remove pmap 194 va=0x6533a000
    if (pmaps[194]) {
        pmap_remove(pmaps[194], 1697882112);
    }
    ops_count++;
    // Op 1545: Remove pmap 149 va=0x7b31e000
    if (pmaps[149]) {
        pmap_remove(pmaps[149], 2066866176);
    }
    ops_count++;
    // Op 1546: Destroy pmap 161
    if (pmaps[161]) {
        pmap_destroy(pmaps[161]);
        pmaps[161] = 0;
    }
    ops_count++;
    // Op 1547: Protect pmap 174 va=0x7da57000
    if (pmaps[174]) {
        pmap_protect(pmaps[174], 2107994112, 2107998208, 1);
    }
    ops_count++;
    // Op 1548: Remove pmap 186 va=0x728f6000
    if (pmaps[186]) {
        pmap_remove(pmaps[186], 1921998848);
    }
    ops_count++;
    // Op 1549: Enter pmap 174 va=0x5832d000 pa=0x98185000 prot=0x1
    if (pmaps[174]) {
        pmap_enter(pmaps[174], 1479725056, 2551730176, 1, 0);
    }
    ops_count++;
    // Op 1550: Extract pmap 205 va=0x7134b000
    if (pmaps[205]) {
        pmap_extract(pmaps[205], 1899278336);
    }
    ops_count++;
    // Op 1551: Remove pmap 187 va=0xb7d2000
    if (pmaps[187]) {
        pmap_remove(pmaps[187], 192749568);
    }
    ops_count++;
    // Op 1552: Extract pmap 154 va=0x52b45000
    if (pmaps[154]) {
        pmap_extract(pmaps[154], 1387548672);
    }
    ops_count++;
    // Op 1553: Destroy pmap 216
    if (pmaps[216]) {
        pmap_destroy(pmaps[216]);
        pmaps[216] = 0;
    }
    ops_count++;
    // Op 1554: Extract pmap 142 va=0xafd0e000
    if (pmaps[142]) {
        pmap_extract(pmaps[142], 2949701632);
    }
    ops_count++;
    // Op 1555: Extract pmap 146 va=0x49a5c000
    if (pmaps[146]) {
        pmap_extract(pmaps[146], 1235599360);
    }
    ops_count++;
    // Op 1556: Create pmap 232
    pmaps[232] = pmap_create();
    if (!pmaps[232]) kprint("Warning: pmap_create failed for 232\n");
    ops_count++;
    // Op 1557: Remove pmap 142 va=0x7a25f000
    if (pmaps[142]) {
        pmap_remove(pmaps[142], 2049306624);
    }
    ops_count++;
    // Op 1558: Enter pmap 94 va=0x54f5e000 pa=0x3591a000 prot=0x3
    if (pmaps[94]) {
        pmap_enter(pmaps[94], 1425399808, 898736128, 3, 0);
    }
    ops_count++;
    // Op 1559: Destroy pmap 198
    if (pmaps[198]) {
        pmap_destroy(pmaps[198]);
        pmaps[198] = 0;
    }
    ops_count++;
    // Op 1560: Enter pmap 219 va=0x242f6000 pa=0xb2002000 prot=0x1
    if (pmaps[219]) {
        pmap_enter(pmaps[219], 607084544, 2986352640, 1, 0);
    }
    ops_count++;
    // Op 1561: Enter pmap 232 va=0xa9dd0000 pa=0x86ec4000 prot=0x1
    if (pmaps[232]) {
        pmap_enter(pmaps[232], 2849832960, 2263629824, 1, 0);
    }
    ops_count++;
    // Op 1562: Enter pmap 125 va=0x48b52000 pa=0x3370d000 prot=0x5
    if (pmaps[125]) {
        pmap_enter(pmaps[125], 1219829760, 863031296, 5, 0);
    }
    ops_count++;
    // Op 1563: Create pmap 233
    pmaps[233] = pmap_create();
    if (!pmaps[233]) kprint("Warning: pmap_create failed for 233\n");
    ops_count++;
    // Op 1564: Remove pmap 88 va=0xf79b000
    if (pmaps[88]) {
        pmap_remove(pmaps[88], 259633152);
    }
    ops_count++;
    // Op 1565: Enter pmap 166 va=0xa9b93000 pa=0xda321000 prot=0x5
    if (pmaps[166]) {
        pmap_enter(pmaps[166], 2847485952, 3660713984, 5, 0);
    }
    ops_count++;
    // Op 1566: Extract pmap 94 va=0x52d6c000
    if (pmaps[94]) {
        pmap_extract(pmaps[94], 1389805568);
    }
    ops_count++;
    // Op 1567: Destroy pmap 181
    if (pmaps[181]) {
        pmap_destroy(pmaps[181]);
        pmaps[181] = 0;
    }
    ops_count++;
    // Op 1568: Enter pmap 219 va=0x197fb000 pa=0xe7742000 prot=0x5
    if (pmaps[219]) {
        pmap_enter(pmaps[219], 427798528, 3883147264, 5, 0);
    }
    ops_count++;
    // Op 1569: Enter pmap 206 va=0x88146000 pa=0x16988000 prot=0x3
    if (pmaps[206]) {
        pmap_enter(pmaps[206], 2283036672, 379092992, 3, 0);
    }
    ops_count++;
    // Op 1570: Destroy pmap 194
    if (pmaps[194]) {
        pmap_destroy(pmaps[194]);
        pmaps[194] = 0;
    }
    ops_count++;
    // Op 1571: Remove pmap 220 va=0x64f26000
    if (pmaps[220]) {
        pmap_remove(pmaps[220], 1693605888);
    }
    ops_count++;
    // Op 1572: Create pmap 234
    pmaps[234] = pmap_create();
    if (!pmaps[234]) kprint("Warning: pmap_create failed for 234\n");
    ops_count++;
    // Op 1573: Extract pmap 173 va=0x8a38e000
    if (pmaps[173]) {
        pmap_extract(pmaps[173], 2318983168);
    }
    ops_count++;
    // Op 1574: Protect pmap 94 va=0x6ac2000
    if (pmaps[94]) {
        pmap_protect(pmaps[94], 111943680, 111947776, 15);
    }
    ops_count++;
    // Op 1575: Enter pmap 173 va=0x3c77f000 pa=0xff097000 prot=0x5
    if (pmaps[173]) {
        pmap_enter(pmaps[173], 1014493184, 4278808576, 5, 0);
    }
    ops_count++;
    // Op 1576: Protect pmap 210 va=0x38863000
    if (pmaps[210]) {
        pmap_protect(pmaps[210], 948318208, 948322304, 15);
    }
    ops_count++;
    // Op 1577: Extract pmap 229 va=0x8e109000
    if (pmaps[229]) {
        pmap_extract(pmaps[229], 2383450112);
    }
    ops_count++;
    // Op 1578: Extract pmap 154 va=0x52b45000
    if (pmaps[154]) {
        pmap_extract(pmaps[154], 1387548672);
    }
    ops_count++;
    // Op 1579: Remove pmap 133 va=0xb7172000
    if (pmaps[133]) {
        pmap_remove(pmaps[133], 3071746048);
    }
    ops_count++;
    // Op 1580: Enter pmap 217 va=0x9cc97000 pa=0xdf795000 prot=0x3
    if (pmaps[217]) {
        pmap_enter(pmaps[217], 2630447104, 3749269504, 3, 0);
    }
    ops_count++;
    // Op 1581: Enter pmap 205 va=0x271aa000 pa=0x6d3cc000 prot=0x5
    if (pmaps[205]) {
        pmap_enter(pmaps[205], 656056320, 1832697856, 5, 0);
    }
    ops_count++;
    // Op 1582: Extract pmap 175 va=0x7008000
    if (pmaps[175]) {
        pmap_extract(pmaps[175], 117473280);
    }
    ops_count++;
    // Op 1583: Destroy pmap 144
    if (pmaps[144]) {
        pmap_destroy(pmaps[144]);
        pmaps[144] = 0;
    }
    ops_count++;
    // Op 1584: Remove pmap 219 va=0x242f6000
    if (pmaps[219]) {
        pmap_remove(pmaps[219], 607084544);
    }
    ops_count++;
    // Op 1585: Extract pmap 174 va=0x7da57000
    if (pmaps[174]) {
        pmap_extract(pmaps[174], 2107994112);
    }
    ops_count++;
    // Op 1586: Enter pmap 227 va=0x2528c000 pa=0xa80ec000 prot=0x3
    if (pmaps[227]) {
        pmap_enter(pmaps[227], 623427584, 2819538944, 3, 0);
    }
    ops_count++;
    // Op 1587: Enter pmap 205 va=0x88703000 pa=0xf2e78000 prot=0x1
    if (pmaps[205]) {
        pmap_enter(pmaps[205], 2289053696, 4075257856, 1, 0);
    }
    ops_count++;
    // Op 1588: Destroy pmap 231
    if (pmaps[231]) {
        pmap_destroy(pmaps[231]);
        pmaps[231] = 0;
    }
    ops_count++;
    // Op 1589: Extract pmap 223 va=0x51959000
    if (pmaps[223]) {
        pmap_extract(pmaps[223], 1368756224);
    }
    ops_count++;
    // Op 1590: Extract pmap 187 va=0x4da84000
    if (pmaps[187]) {
        pmap_extract(pmaps[187], 1302872064);
    }
    ops_count++;
    // Op 1591: Enter pmap 230 va=0xa3087000 pa=0xeef97000 prot=0x5
    if (pmaps[230]) {
        pmap_enter(pmaps[230], 2735239168, 4009324544, 5, 0);
    }
    ops_count++;
    // Op 1592: Destroy pmap 153
    if (pmaps[153]) {
        pmap_destroy(pmaps[153]);
        pmaps[153] = 0;
    }
    ops_count++;
    // Op 1593: Create pmap 235
    pmaps[235] = pmap_create();
    if (!pmaps[235]) kprint("Warning: pmap_create failed for 235\n");
    ops_count++;
    // Op 1594: Destroy pmap 227
    if (pmaps[227]) {
        pmap_destroy(pmaps[227]);
        pmaps[227] = 0;
    }
    ops_count++;
    // Op 1595: Extract pmap 212 va=0x9129c000
    if (pmaps[212]) {
        pmap_extract(pmaps[212], 2435432448);
    }
    ops_count++;
    // Op 1596: Remove pmap 210 va=0x38863000
    if (pmaps[210]) {
        pmap_remove(pmaps[210], 948318208);
    }
    ops_count++;
    // Op 1597: Destroy pmap 94
    if (pmaps[94]) {
        pmap_destroy(pmaps[94]);
        pmaps[94] = 0;
    }
    ops_count++;
    // Op 1598: Enter pmap 223 va=0x7a02a000 pa=0x22638000 prot=0xf
    if (pmaps[223]) {
        pmap_enter(pmaps[223], 2046992384, 576946176, 15, 0);
    }
    ops_count++;
    // Op 1599: Destroy pmap 187
    if (pmaps[187]) {
        pmap_destroy(pmaps[187]);
        pmaps[187] = 0;
    }
    ops_count++;
    kprint(".");
    // Op 1600: Enter pmap 147 va=0x29edf000 pa=0x31897000 prot=0x3
    if (pmaps[147]) {
        pmap_enter(pmaps[147], 703459328, 831090688, 3, 0);
    }
    ops_count++;
    // Op 1601: Create pmap 236
    pmaps[236] = pmap_create();
    if (!pmaps[236]) kprint("Warning: pmap_create failed for 236\n");
    ops_count++;
    // Op 1602: Protect pmap 88 va=0x4e2d7000
    if (pmaps[88]) {
        pmap_protect(pmaps[88], 1311600640, 1311604736, 15);
    }
    ops_count++;
    // Op 1603: Destroy pmap 222
    if (pmaps[222]) {
        pmap_destroy(pmaps[222]);
        pmaps[222] = 0;
    }
    ops_count++;
    // Op 1604: Extract pmap 232 va=0xa9dd0000
    if (pmaps[232]) {
        pmap_extract(pmaps[232], 2849832960);
    }
    ops_count++;
    // Op 1605: Extract pmap 200 va=0x7d6cf000
    if (pmaps[200]) {
        pmap_extract(pmaps[200], 2104291328);
    }
    ops_count++;
    // Op 1606: Remove pmap 201 va=0x3f2d4000
    if (pmaps[201]) {
        pmap_remove(pmaps[201], 1059930112);
    }
    ops_count++;
    // Op 1607: Enter pmap 233 va=0xab53000 pa=0x9bb14000 prot=0xf
    if (pmaps[233]) {
        pmap_enter(pmaps[233], 179646464, 2612084736, 15, 0);
    }
    ops_count++;
    // Op 1608: Protect pmap 196 va=0x69864000
    if (pmaps[196]) {
        pmap_protect(pmaps[196], 1770405888, 1770409984, 1);
    }
    ops_count++;
    // Op 1609: Enter pmap 236 va=0xadbd000 pa=0xe0c62000 prot=0x3
    if (pmaps[236]) {
        pmap_enter(pmaps[236], 182177792, 3771080704, 3, 0);
    }
    ops_count++;
    // Op 1610: Enter pmap 196 va=0xbc342000 pa=0xa1c6000 prot=0xf
    if (pmaps[196]) {
        pmap_enter(pmaps[196], 3157532672, 169631744, 15, 0);
    }
    ops_count++;
    // Op 1611: Enter pmap 219 va=0xc9eb000 pa=0x30aa0000 prot=0x5
    if (pmaps[219]) {
        pmap_enter(pmaps[219], 211726336, 816447488, 5, 0);
    }
    ops_count++;
    // Op 1612: Enter pmap 177 va=0x4931c000 pa=0x762ad000 prot=0x3
    if (pmaps[177]) {
        pmap_enter(pmaps[177], 1227997184, 1982517248, 3, 0);
    }
    ops_count++;
    // Op 1613: Enter pmap 217 va=0xdf6d000 pa=0xb0b0c000 prot=0x3
    if (pmaps[217]) {
        pmap_enter(pmaps[217], 234278912, 2964373504, 3, 0);
    }
    ops_count++;
    // Op 1614: Enter pmap 220 va=0x73a28000 pa=0x17372000 prot=0x1
    if (pmaps[220]) {
        pmap_enter(pmaps[220], 1940029440, 389488640, 1, 0);
    }
    ops_count++;
    // Op 1615: Destroy pmap 212
    if (pmaps[212]) {
        pmap_destroy(pmaps[212]);
        pmaps[212] = 0;
    }
    ops_count++;
    // Op 1616: Extract pmap 228 va=0x64a01000
    if (pmaps[228]) {
        pmap_extract(pmaps[228], 1688211456);
    }
    ops_count++;
    // Op 1617: Enter pmap 200 va=0x660b000 pa=0x2ddaf000 prot=0xf
    if (pmaps[200]) {
        pmap_enter(pmaps[200], 106999808, 769323008, 15, 0);
    }
    ops_count++;
    // Op 1618: Remove pmap 229 va=0x33b1e000
    if (pmaps[229]) {
        pmap_remove(pmaps[229], 867295232);
    }
    ops_count++;
    // Op 1619: Extract pmap 141 va=0x4530000
    if (pmaps[141]) {
        pmap_extract(pmaps[141], 72548352);
    }
    ops_count++;
    // Op 1620: Remove pmap 229 va=0x29fb9000
    if (pmaps[229]) {
        pmap_remove(pmaps[229], 704352256);
    }
    ops_count++;
    // Op 1621: Protect pmap 146 va=0x5bf7e000
    if (pmaps[146]) {
        pmap_protect(pmaps[146], 1542971392, 1542975488, 1);
    }
    ops_count++;
    // Op 1622: Protect pmap 220 va=0x73a28000
    if (pmaps[220]) {
        pmap_protect(pmaps[220], 1940029440, 1940033536, 1);
    }
    ops_count++;
    // Op 1623: Enter pmap 192 va=0xba147000 pa=0xca935000 prot=0x3
    if (pmaps[192]) {
        pmap_enter(pmaps[192], 3121901568, 3398651904, 3, 0);
    }
    ops_count++;
    // Op 1624: Enter pmap 192 va=0xb3d7000 pa=0xc4bcb000 prot=0x3
    if (pmaps[192]) {
        pmap_enter(pmaps[192], 188575744, 3300700160, 3, 0);
    }
    ops_count++;
    // Op 1625: Extract pmap 210 va=0x266c5000
    if (pmaps[210]) {
        pmap_extract(pmaps[210], 644632576);
    }
    ops_count++;
    // Op 1626: Destroy pmap 148
    if (pmaps[148]) {
        pmap_destroy(pmaps[148]);
        pmaps[148] = 0;
    }
    ops_count++;
    // Op 1627: Destroy pmap 230
    if (pmaps[230]) {
        pmap_destroy(pmaps[230]);
        pmaps[230] = 0;
    }
    ops_count++;
    // Op 1628: Create pmap 237
    pmaps[237] = pmap_create();
    if (!pmaps[237]) kprint("Warning: pmap_create failed for 237\n");
    ops_count++;
    // Op 1629: Enter pmap 233 va=0x723e0000 pa=0x49fd8000 prot=0x3
    if (pmaps[233]) {
        pmap_enter(pmaps[233], 1916665856, 1241350144, 3, 0);
    }
    ops_count++;
    // Op 1630: Create pmap 238
    pmaps[238] = pmap_create();
    if (!pmaps[238]) kprint("Warning: pmap_create failed for 238\n");
    ops_count++;
    // Op 1631: Create pmap 239
    pmaps[239] = pmap_create();
    if (!pmaps[239]) kprint("Warning: pmap_create failed for 239\n");
    ops_count++;
    // Op 1632: Destroy pmap 220
    if (pmaps[220]) {
        pmap_destroy(pmaps[220]);
        pmaps[220] = 0;
    }
    ops_count++;
    // Op 1633: Protect pmap 88 va=0x95411000
    if (pmaps[88]) {
        pmap_protect(pmaps[88], 2504069120, 2504073216, 15);
    }
    ops_count++;
    // Op 1634: Extract pmap 209 va=0x9ba4e000
    if (pmaps[209]) {
        pmap_extract(pmaps[209], 2611273728);
    }
    ops_count++;
    // Op 1635: Extract pmap 173 va=0x3c77f000
    if (pmaps[173]) {
        pmap_extract(pmaps[173], 1014493184);
    }
    ops_count++;
    // Op 1636: Enter pmap 201 va=0x1453f000 pa=0x99f95000 prot=0x1
    if (pmaps[201]) {
        pmap_enter(pmaps[201], 341045248, 2583252992, 1, 0);
    }
    ops_count++;
    // Op 1637: Destroy pmap 210
    if (pmaps[210]) {
        pmap_destroy(pmaps[210]);
        pmaps[210] = 0;
    }
    ops_count++;
    // Op 1638: Enter pmap 226 va=0x8ebb9000 pa=0xd2037000 prot=0x3
    if (pmaps[226]) {
        pmap_enter(pmaps[226], 2394656768, 3523440640, 3, 0);
    }
    ops_count++;
    // Op 1639: Create pmap 240
    pmaps[240] = pmap_create();
    if (!pmaps[240]) kprint("Warning: pmap_create failed for 240\n");
    ops_count++;
    // Op 1640: Create pmap 241
    pmaps[241] = pmap_create();
    if (!pmaps[241]) kprint("Warning: pmap_create failed for 241\n");
    ops_count++;
    // Op 1641: Remove pmap 125 va=0x48b52000
    if (pmaps[125]) {
        pmap_remove(pmaps[125], 1219829760);
    }
    ops_count++;
    // Op 1642: Extract pmap 229 va=0x9a70f000
    if (pmaps[229]) {
        pmap_extract(pmaps[229], 2591092736);
    }
    ops_count++;
    // Op 1643: Enter pmap 141 va=0xc8d6000 pa=0x1bf00000 prot=0xf
    if (pmaps[141]) {
        pmap_enter(pmaps[141], 210591744, 468713472, 15, 0);
    }
    ops_count++;
    // Op 1644: Extract pmap 154 va=0xa237b000
    if (pmaps[154]) {
        pmap_extract(pmaps[154], 2721558528);
    }
    ops_count++;
    // Op 1645: Extract pmap 236 va=0xadbd000
    if (pmaps[236]) {
        pmap_extract(pmaps[236], 182177792);
    }
    ops_count++;
    // Op 1646: Create pmap 242
    pmaps[242] = pmap_create();
    if (!pmaps[242]) kprint("Warning: pmap_create failed for 242\n");
    ops_count++;
    // Op 1647: Enter pmap 192 va=0x61023000 pa=0xd20cd000 prot=0x1
    if (pmaps[192]) {
        pmap_enter(pmaps[192], 1627533312, 3524055040, 1, 0);
    }
    ops_count++;
    // Op 1648: Remove pmap 177 va=0x4931c000
    if (pmaps[177]) {
        pmap_remove(pmaps[177], 1227997184);
    }
    ops_count++;
    // Op 1649: Create pmap 243
    pmaps[243] = pmap_create();
    if (!pmaps[243]) kprint("Warning: pmap_create failed for 243\n");
    ops_count++;
    // Op 1650: Remove pmap 175 va=0x3f461000
    if (pmaps[175]) {
        pmap_remove(pmaps[175], 1061556224);
    }
    ops_count++;
    // Op 1651: Enter pmap 207 va=0x35b87000 pa=0xb231e000 prot=0x3
    if (pmaps[207]) {
        pmap_enter(pmaps[207], 901279744, 2989613056, 3, 0);
    }
    ops_count++;
    // Op 1652: Remove pmap 142 va=0xafd0e000
    if (pmaps[142]) {
        pmap_remove(pmaps[142], 2949701632);
    }
    ops_count++;
    // Op 1653: Enter pmap 233 va=0x9ad2c000 pa=0xef238000 prot=0x5
    if (pmaps[233]) {
        pmap_enter(pmaps[233], 2597502976, 4012081152, 5, 0);
    }
    ops_count++;
    // Op 1654: Enter pmap 243 va=0x51057000 pa=0x42891000 prot=0x5
    if (pmaps[243]) {
        pmap_enter(pmaps[243], 1359310848, 1116278784, 5, 0);
    }
    ops_count++;
    // Op 1655: Extract pmap 225 va=0x850b8000
    if (pmaps[225]) {
        pmap_extract(pmaps[225], 2232123392);
    }
    ops_count++;
    // Op 1656: Protect pmap 205 va=0x271aa000
    if (pmaps[205]) {
        pmap_protect(pmaps[205], 656056320, 656060416, 1);
    }
    ops_count++;
    // Op 1657: Extract pmap 168 va=0x318ce000
    if (pmaps[168]) {
        pmap_extract(pmaps[168], 831315968);
    }
    ops_count++;
    // Op 1658: Create pmap 244
    pmaps[244] = pmap_create();
    if (!pmaps[244]) kprint("Warning: pmap_create failed for 244\n");
    ops_count++;
    // Op 1659: Create pmap 245
    pmaps[245] = pmap_create();
    if (!pmaps[245]) kprint("Warning: pmap_create failed for 245\n");
    ops_count++;
    // Op 1660: Remove pmap 146 va=0x5bf7e000
    if (pmaps[146]) {
        pmap_remove(pmaps[146], 1542971392);
    }
    ops_count++;
    // Op 1661: Enter pmap 185 va=0x5982b000 pa=0x8f876000 prot=0x3
    if (pmaps[185]) {
        pmap_enter(pmaps[185], 1501736960, 2408013824, 3, 0);
    }
    ops_count++;
    // Op 1662: Create pmap 246
    pmaps[246] = pmap_create();
    if (!pmaps[246]) kprint("Warning: pmap_create failed for 246\n");
    ops_count++;
    // Op 1663: Destroy pmap 177
    if (pmaps[177]) {
        pmap_destroy(pmaps[177]);
        pmaps[177] = 0;
    }
    ops_count++;
    // Op 1664: Enter pmap 133 va=0x4eca3000 pa=0x4cef6000 prot=0x5
    if (pmaps[133]) {
        pmap_enter(pmaps[133], 1321873408, 1290756096, 5, 0);
    }
    ops_count++;
    // Op 1665: Remove pmap 186 va=0x65638000
    if (pmaps[186]) {
        pmap_remove(pmaps[186], 1701019648);
    }
    ops_count++;
    // Op 1666: Protect pmap 226 va=0x8ebb9000
    if (pmaps[226]) {
        pmap_protect(pmaps[226], 2394656768, 2394660864, 1);
    }
    ops_count++;
    // Op 1667: Destroy pmap 243
    if (pmaps[243]) {
        pmap_destroy(pmaps[243]);
        pmaps[243] = 0;
    }
    ops_count++;
    // Op 1668: Enter pmap 233 va=0x7aa6e000 pa=0xea99a000 prot=0xf
    if (pmaps[233]) {
        pmap_enter(pmaps[233], 2057756672, 3935936512, 15, 0);
    }
    ops_count++;
    // Op 1669: Create pmap 247
    pmaps[247] = pmap_create();
    if (!pmaps[247]) kprint("Warning: pmap_create failed for 247\n");
    ops_count++;
    // Op 1670: Destroy pmap 204
    if (pmaps[204]) {
        pmap_destroy(pmaps[204]);
        pmaps[204] = 0;
    }
    ops_count++;
    // Op 1671: Extract pmap 217 va=0x76648000
    if (pmaps[217]) {
        pmap_extract(pmaps[217], 1986297856);
    }
    ops_count++;
    // Op 1672: Protect pmap 200 va=0x66593000
    if (pmaps[200]) {
        pmap_protect(pmaps[200], 1717121024, 1717125120, 15);
    }
    ops_count++;
    // Op 1673: Remove pmap 234 va=0x50c4d000
    if (pmaps[234]) {
        pmap_remove(pmaps[234], 1355075584);
    }
    ops_count++;
    // Op 1674: Remove pmap 115 va=0x93871000
    if (pmaps[115]) {
        pmap_remove(pmaps[115], 2475102208);
    }
    ops_count++;
    // Op 1675: Remove pmap 186 va=0x5a854000
    if (pmaps[186]) {
        pmap_remove(pmaps[186], 1518682112);
    }
    ops_count++;
    // Op 1676: Create pmap 248
    pmaps[248] = pmap_create();
    if (!pmaps[248]) kprint("Warning: pmap_create failed for 248\n");
    ops_count++;
    // Op 1677: Create pmap 249
    pmaps[249] = pmap_create();
    if (!pmaps[249]) kprint("Warning: pmap_create failed for 249\n");
    ops_count++;
    // Op 1678: Extract pmap 229 va=0x2a7d0000
    if (pmaps[229]) {
        pmap_extract(pmaps[229], 712835072);
    }
    ops_count++;
    // Op 1679: Remove pmap 197 va=0x2ff82000
    if (pmaps[197]) {
        pmap_remove(pmaps[197], 804790272);
    }
    ops_count++;
    // Op 1680: Remove pmap 247 va=0x4eeb9000
    if (pmaps[247]) {
        pmap_remove(pmaps[247], 1324060672);
    }
    ops_count++;
    // Op 1681: Protect pmap 185 va=0x511ef000
    if (pmaps[185]) {
        pmap_protect(pmaps[185], 1360982016, 1360986112, 15);
    }
    ops_count++;
    // Op 1682: Enter pmap 207 va=0x3cd62000 pa=0x5f301000 prot=0xf
    if (pmaps[207]) {
        pmap_enter(pmaps[207], 1020665856, 1596985344, 15, 0);
    }
    ops_count++;
    // Op 1683: Protect pmap 184 va=0x9256e000
    if (pmaps[184]) {
        pmap_protect(pmaps[184], 2455166976, 2455171072, 15);
    }
    ops_count++;
    // Op 1684: Destroy pmap 196
    if (pmaps[196]) {
        pmap_destroy(pmaps[196]);
        pmaps[196] = 0;
    }
    ops_count++;
    // Op 1685: Destroy pmap 141
    if (pmaps[141]) {
        pmap_destroy(pmaps[141]);
        pmaps[141] = 0;
    }
    ops_count++;
    // Op 1686: Enter pmap 115 va=0x7b8fe000 pa=0xe7bb2000 prot=0x3
    if (pmaps[115]) {
        pmap_enter(pmaps[115], 2073026560, 3887800320, 3, 0);
    }
    ops_count++;
    // Op 1687: Destroy pmap 209
    if (pmaps[209]) {
        pmap_destroy(pmaps[209]);
        pmaps[209] = 0;
    }
    ops_count++;
    // Op 1688: Protect pmap 223 va=0x7a02a000
    if (pmaps[223]) {
        pmap_protect(pmaps[223], 2046992384, 2046996480, 15);
    }
    ops_count++;
    // Op 1689: Create pmap 250
    pmaps[250] = pmap_create();
    if (!pmaps[250]) kprint("Warning: pmap_create failed for 250\n");
    ops_count++;
    // Op 1690: Extract pmap 247 va=0x84223000
    if (pmaps[247]) {
        pmap_extract(pmaps[247], 2216833024);
    }
    ops_count++;
    // Op 1691: Remove pmap 149 va=0x86b49000
    if (pmaps[149]) {
        pmap_remove(pmaps[149], 2259980288);
    }
    ops_count++;
    // Op 1692: Protect pmap 207 va=0x35b87000
    if (pmaps[207]) {
        pmap_protect(pmaps[207], 901279744, 901283840, 1);
    }
    ops_count++;
    // Op 1693: Create pmap 251
    pmaps[251] = pmap_create();
    if (!pmaps[251]) kprint("Warning: pmap_create failed for 251\n");
    ops_count++;
    // Op 1694: Extract pmap 237 va=0x164aa000
    if (pmaps[237]) {
        pmap_extract(pmaps[237], 373989376);
    }
    ops_count++;
    // Op 1695: Enter pmap 251 va=0xbcfe000 pa=0x1a74f000 prot=0x5
    if (pmaps[251]) {
        pmap_enter(pmaps[251], 198172672, 443871232, 5, 0);
    }
    ops_count++;
    // Op 1696: Create pmap 252
    pmaps[252] = pmap_create();
    if (!pmaps[252]) kprint("Warning: pmap_create failed for 252\n");
    ops_count++;
    // Op 1697: Extract pmap 186 va=0x495c8000
    if (pmaps[186]) {
        pmap_extract(pmaps[186], 1230798848);
    }
    ops_count++;
    // Op 1698: Enter pmap 186 va=0x70ed0000 pa=0xabdec000 prot=0x1
    if (pmaps[186]) {
        pmap_enter(pmaps[186], 1894580224, 2883502080, 1, 0);
    }
    ops_count++;
    // Op 1699: Enter pmap 155 va=0x460d2000 pa=0x1d5d4000 prot=0x1
    if (pmaps[155]) {
        pmap_enter(pmaps[155], 1175265280, 492650496, 1, 0);
    }
    ops_count++;
    kprint(".");
    // Op 1700: Extract pmap 226 va=0x8ebb9000
    if (pmaps[226]) {
        pmap_extract(pmaps[226], 2394656768);
    }
    ops_count++;
    // Op 1701: Enter pmap 226 va=0x51884000 pa=0x4573a000 prot=0x3
    if (pmaps[226]) {
        pmap_enter(pmaps[226], 1367883776, 1165205504, 3, 0);
    }
    ops_count++;
    // Op 1702: Extract pmap 246 va=0x3eace000
    if (pmaps[246]) {
        pmap_extract(pmaps[246], 1051516928);
    }
    ops_count++;
    // Op 1703: Destroy pmap 185
    if (pmaps[185]) {
        pmap_destroy(pmaps[185]);
        pmaps[185] = 0;
    }
    ops_count++;
    // Op 1704: Remove pmap 197 va=0x8a635000
    if (pmaps[197]) {
        pmap_remove(pmaps[197], 2321764352);
    }
    ops_count++;
    // Op 1705: Enter pmap 149 va=0x76c2000 pa=0x78922000 prot=0x1
    if (pmaps[149]) {
        pmap_enter(pmaps[149], 124526592, 2022842368, 1, 0);
    }
    ops_count++;
    // Op 1706: Create pmap 253
    pmaps[253] = pmap_create();
    if (!pmaps[253]) kprint("Warning: pmap_create failed for 253\n");
    ops_count++;
    // Op 1707: Create pmap 254
    pmaps[254] = pmap_create();
    if (!pmaps[254]) kprint("Warning: pmap_create failed for 254\n");
    ops_count++;
    // Op 1708: Enter pmap 159 va=0xb465d000 pa=0x98ab6000 prot=0xf
    if (pmaps[159]) {
        pmap_enter(pmaps[159], 3026571264, 2561368064, 15, 0);
    }
    ops_count++;
    // Op 1709: Remove pmap 186 va=0x70ed0000
    if (pmaps[186]) {
        pmap_remove(pmaps[186], 1894580224);
    }
    ops_count++;
    // Op 1710: Enter pmap 244 va=0x7737d000 pa=0x916aa000 prot=0x3
    if (pmaps[244]) {
        pmap_enter(pmaps[244], 2000146432, 2439684096, 3, 0);
    }
    ops_count++;
    // Op 1711: Create pmap 255
    pmaps[255] = pmap_create();
    if (!pmaps[255]) kprint("Warning: pmap_create failed for 255\n");
    ops_count++;
    // Op 1712: Enter pmap 213 va=0x4f75e000 pa=0xaf49f000 prot=0xf
    if (pmaps[213]) {
        pmap_enter(pmaps[213], 1333125120, 2940858368, 15, 0);
    }
    ops_count++;
    // Op 1713: Enter pmap 173 va=0x24cb0000 pa=0xdc80c000 prot=0x5
    if (pmaps[173]) {
        pmap_enter(pmaps[173], 617283584, 3699425280, 5, 0);
    }
    ops_count++;
    // Op 1714: Extract pmap 202 va=0x71694000
    if (pmaps[202]) {
        pmap_extract(pmaps[202], 1902723072);
    }
    ops_count++;
    // Op 1715: Enter pmap 219 va=0x7bf7000 pa=0x10cbc000 prot=0xf
    if (pmaps[219]) {
        pmap_enter(pmaps[219], 129986560, 281788416, 15, 0);
    }
    ops_count++;
    // Op 1716: Create pmap 256
    pmaps[256] = pmap_create();
    if (!pmaps[256]) kprint("Warning: pmap_create failed for 256\n");
    ops_count++;
    // Op 1717: Enter pmap 250 va=0x2bb9b000 pa=0x48520000 prot=0x5
    if (pmaps[250]) {
        pmap_enter(pmaps[250], 733589504, 1213333504, 5, 0);
    }
    ops_count++;
    // Op 1718: Remove pmap 149 va=0x76c2000
    if (pmaps[149]) {
        pmap_remove(pmaps[149], 124526592);
    }
    ops_count++;
    // Op 1719: Enter pmap 240 va=0x1b633000 pa=0x537c3000 prot=0x1
    if (pmaps[240]) {
        pmap_enter(pmaps[240], 459485184, 1400647680, 1, 0);
    }
    ops_count++;
    // Op 1720: Protect pmap 226 va=0x51884000
    if (pmaps[226]) {
        pmap_protect(pmaps[226], 1367883776, 1367887872, 15);
    }
    ops_count++;
    // Op 1721: Remove pmap 174 va=0x7da57000
    if (pmaps[174]) {
        pmap_remove(pmaps[174], 2107994112);
    }
    ops_count++;
    // Op 1722: Enter pmap 217 va=0x6241d000 pa=0x10f94000 prot=0x5
    if (pmaps[217]) {
        pmap_enter(pmaps[217], 1648480256, 284770304, 5, 0);
    }
    ops_count++;
    // Op 1723: Destroy pmap 239
    if (pmaps[239]) {
        pmap_destroy(pmaps[239]);
        pmaps[239] = 0;
    }
    ops_count++;
    // Op 1724: Remove pmap 247 va=0x6f736000
    if (pmaps[247]) {
        pmap_remove(pmaps[247], 1869832192);
    }
    ops_count++;
    // Op 1725: Remove pmap 197 va=0x8b662000
    if (pmaps[197]) {
        pmap_remove(pmaps[197], 2338725888);
    }
    ops_count++;
    // Op 1726: Remove pmap 159 va=0xb465d000
    if (pmaps[159]) {
        pmap_remove(pmaps[159], 3026571264);
    }
    ops_count++;
    // Op 1727: Remove pmap 244 va=0x7737d000
    if (pmaps[244]) {
        pmap_remove(pmaps[244], 2000146432);
    }
    ops_count++;
    // Op 1728: Create pmap 257
    pmaps[257] = pmap_create();
    if (!pmaps[257]) kprint("Warning: pmap_create failed for 257\n");
    ops_count++;
    // Op 1729: Remove pmap 192 va=0x61023000
    if (pmaps[192]) {
        pmap_remove(pmaps[192], 1627533312);
    }
    ops_count++;
    // Op 1730: Create pmap 258
    pmaps[258] = pmap_create();
    if (!pmaps[258]) kprint("Warning: pmap_create failed for 258\n");
    ops_count++;
    // Op 1731: Enter pmap 186 va=0x67e22000 pa=0x11e1c000 prot=0x1
    if (pmaps[186]) {
        pmap_enter(pmaps[186], 1742872576, 300007424, 1, 0);
    }
    ops_count++;
    // Op 1732: Remove pmap 233 va=0x9ad2c000
    if (pmaps[233]) {
        pmap_remove(pmaps[233], 2597502976);
    }
    ops_count++;
    // Op 1733: Enter pmap 236 va=0xa9009000 pa=0xfcd4000 prot=0x1
    if (pmaps[236]) {
        pmap_enter(pmaps[236], 2835386368, 265109504, 1, 0);
    }
    ops_count++;
    // Op 1734: Destroy pmap 149
    if (pmaps[149]) {
        pmap_destroy(pmaps[149]);
        pmaps[149] = 0;
    }
    ops_count++;
    // Op 1735: Extract pmap 255 va=0xc2000
    if (pmaps[255]) {
        pmap_extract(pmaps[255], 794624);
    }
    ops_count++;
    // Op 1736: Create pmap 259
    pmaps[259] = pmap_create();
    if (!pmaps[259]) kprint("Warning: pmap_create failed for 259\n");
    ops_count++;
    // Op 1737: Enter pmap 202 va=0x9d963000 pa=0x94035000 prot=0xf
    if (pmaps[202]) {
        pmap_enter(pmaps[202], 2643865600, 2483245056, 15, 0);
    }
    ops_count++;
    // Op 1738: Destroy pmap 240
    if (pmaps[240]) {
        pmap_destroy(pmaps[240]);
        pmaps[240] = 0;
    }
    ops_count++;
    // Op 1739: Extract pmap 192 va=0xba147000
    if (pmaps[192]) {
        pmap_extract(pmaps[192], 3121901568);
    }
    ops_count++;
    // Op 1740: Extract pmap 258 va=0x5821c000
    if (pmaps[258]) {
        pmap_extract(pmaps[258], 1478606848);
    }
    ops_count++;
    // Op 1741: Extract pmap 233 va=0x7aa6e000
    if (pmaps[233]) {
        pmap_extract(pmaps[233], 2057756672);
    }
    ops_count++;
    // Op 1742: Enter pmap 154 va=0x4b4fb000 pa=0x100a2000 prot=0x5
    if (pmaps[154]) {
        pmap_enter(pmaps[154], 1263513600, 269099008, 5, 0);
    }
    ops_count++;
    // Op 1743: Extract pmap 129 va=0x8f5e3000
    if (pmaps[129]) {
        pmap_extract(pmaps[129], 2405314560);
    }
    ops_count++;
    // Op 1744: Protect pmap 200 va=0x66593000
    if (pmaps[200]) {
        pmap_protect(pmaps[200], 1717121024, 1717125120, 15);
    }
    ops_count++;
    // Op 1745: Extract pmap 175 va=0x7008000
    if (pmaps[175]) {
        pmap_extract(pmaps[175], 117473280);
    }
    ops_count++;
    // Op 1746: Remove pmap 244 va=0x61fe9000
    if (pmaps[244]) {
        pmap_remove(pmaps[244], 1644072960);
    }
    ops_count++;
    // Op 1747: Enter pmap 207 va=0x8b8cf000 pa=0x36224000 prot=0x3
    if (pmaps[207]) {
        pmap_enter(pmaps[207], 2341269504, 908214272, 3, 0);
    }
    ops_count++;
    // Op 1748: Enter pmap 168 va=0x8e302000 pa=0x6284d000 prot=0x5
    if (pmaps[168]) {
        pmap_enter(pmaps[168], 2385518592, 1652871168, 5, 0);
    }
    ops_count++;
    // Op 1749: Protect pmap 155 va=0x932d2000
    if (pmaps[155]) {
        pmap_protect(pmaps[155], 2469208064, 2469212160, 1);
    }
    ops_count++;
    // Op 1750: Create pmap 260
    pmaps[260] = pmap_create();
    if (!pmaps[260]) kprint("Warning: pmap_create failed for 260\n");
    ops_count++;
    // Op 1751: Create pmap 261
    pmaps[261] = pmap_create();
    if (!pmaps[261]) kprint("Warning: pmap_create failed for 261\n");
    ops_count++;
    // Op 1752: Create pmap 262
    pmaps[262] = pmap_create();
    if (!pmaps[262]) kprint("Warning: pmap_create failed for 262\n");
    ops_count++;
    // Op 1753: Remove pmap 184 va=0x9256e000
    if (pmaps[184]) {
        pmap_remove(pmaps[184], 2455166976);
    }
    ops_count++;
    // Op 1754: Create pmap 263
    pmaps[263] = pmap_create();
    if (!pmaps[263]) kprint("Warning: pmap_create failed for 263\n");
    ops_count++;
    // Op 1755: Extract pmap 253 va=0x74534000
    if (pmaps[253]) {
        pmap_extract(pmaps[253], 1951612928);
    }
    ops_count++;
    // Op 1756: Enter pmap 256 va=0x627cb000 pa=0x5b056000 prot=0x3
    if (pmaps[256]) {
        pmap_enter(pmaps[256], 1652338688, 1527078912, 3, 0);
    }
    ops_count++;
    // Op 1757: Destroy pmap 88
    if (pmaps[88]) {
        pmap_destroy(pmaps[88]);
        pmaps[88] = 0;
    }
    ops_count++;
    // Op 1758: Remove pmap 223 va=0x7a02a000
    if (pmaps[223]) {
        pmap_remove(pmaps[223], 2046992384);
    }
    ops_count++;
    // Op 1759: Enter pmap 246 va=0x8910000 pa=0xfa7d8000 prot=0x1
    if (pmaps[246]) {
        pmap_enter(pmaps[246], 143720448, 4202528768, 1, 0);
    }
    ops_count++;
    // Op 1760: Remove pmap 247 va=0x1cf86000
    if (pmaps[247]) {
        pmap_remove(pmaps[247], 486039552);
    }
    ops_count++;
    // Op 1761: Remove pmap 166 va=0xa9b93000
    if (pmaps[166]) {
        pmap_remove(pmaps[166], 2847485952);
    }
    ops_count++;
    // Op 1762: Protect pmap 232 va=0xa9dd0000
    if (pmaps[232]) {
        pmap_protect(pmaps[232], 2849832960, 2849837056, 1);
    }
    ops_count++;
    // Op 1763: Enter pmap 168 va=0x3ec78000 pa=0x79029000 prot=0x1
    if (pmaps[168]) {
        pmap_enter(pmaps[168], 1053261824, 2030211072, 1, 0);
    }
    ops_count++;
    // Op 1764: Remove pmap 236 va=0xadbd000
    if (pmaps[236]) {
        pmap_remove(pmaps[236], 182177792);
    }
    ops_count++;
    // Op 1765: Extract pmap 125 va=0x833df000
    if (pmaps[125]) {
        pmap_extract(pmaps[125], 2201874432);
    }
    ops_count++;
    // Op 1766: Extract pmap 241 va=0x4b6c2000
    if (pmaps[241]) {
        pmap_extract(pmaps[241], 1265377280);
    }
    ops_count++;
    // Op 1767: Enter pmap 142 va=0xb1eee000 pa=0xec50d000 prot=0x5
    if (pmaps[142]) {
        pmap_enter(pmaps[142], 2985222144, 3964719104, 5, 0);
    }
    ops_count++;
    // Op 1768: Create pmap 264
    pmaps[264] = pmap_create();
    if (!pmaps[264]) kprint("Warning: pmap_create failed for 264\n");
    ops_count++;
    // Op 1769: Enter pmap 200 va=0x92a70000 pa=0xd46f000 prot=0x5
    if (pmaps[200]) {
        pmap_enter(pmaps[200], 2460418048, 222752768, 5, 0);
    }
    ops_count++;
    // Op 1770: Protect pmap 133 va=0x4eca3000
    if (pmaps[133]) {
        pmap_protect(pmaps[133], 1321873408, 1321877504, 15);
    }
    ops_count++;
    // Op 1771: Create pmap 265
    pmaps[265] = pmap_create();
    if (!pmaps[265]) kprint("Warning: pmap_create failed for 265\n");
    ops_count++;
    // Op 1772: Enter pmap 232 va=0x40867000 pa=0x21fe0000 prot=0x5
    if (pmaps[232]) {
        pmap_enter(pmaps[232], 1082552320, 570294272, 5, 0);
    }
    ops_count++;
    // Op 1773: Enter pmap 226 va=0x3a2d000 pa=0xbdde4000 prot=0x1
    if (pmaps[226]) {
        pmap_enter(pmaps[226], 61001728, 3185459200, 1, 0);
    }
    ops_count++;
    // Op 1774: Remove pmap 197 va=0xa8be7000
    if (pmaps[197]) {
        pmap_remove(pmaps[197], 2831052800);
    }
    ops_count++;
    // Op 1775: Enter pmap 201 va=0x42e18000 pa=0xc863b000 prot=0x3
    if (pmaps[201]) {
        pmap_enter(pmaps[201], 1122074624, 3361976320, 3, 0);
    }
    ops_count++;
    // Op 1776: Enter pmap 142 va=0x57c4f000 pa=0xfbfc1000 prot=0x1
    if (pmaps[142]) {
        pmap_enter(pmaps[142], 1472524288, 4227600384, 1, 0);
    }
    ops_count++;
    // Op 1777: Extract pmap 197 va=0x69b70000
    if (pmaps[197]) {
        pmap_extract(pmaps[197], 1773600768);
    }
    ops_count++;
    // Op 1778: Remove pmap 200 va=0x66593000
    if (pmaps[200]) {
        pmap_remove(pmaps[200], 1717121024);
    }
    ops_count++;
    // Op 1779: Remove pmap 244 va=0xde85000
    if (pmaps[244]) {
        pmap_remove(pmaps[244], 233328640);
    }
    ops_count++;
    // Op 1780: Remove pmap 175 va=0x7008000
    if (pmaps[175]) {
        pmap_remove(pmaps[175], 117473280);
    }
    ops_count++;
    // Op 1781: Destroy pmap 251
    if (pmaps[251]) {
        pmap_destroy(pmaps[251]);
        pmaps[251] = 0;
    }
    ops_count++;
    // Op 1782: Enter pmap 261 va=0xbf03c000 pa=0xf2d0a000 prot=0x3
    if (pmaps[261]) {
        pmap_enter(pmaps[261], 3204694016, 4073758720, 3, 0);
    }
    ops_count++;
    // Op 1783: Enter pmap 249 va=0x797ec000 pa=0x434b6000 prot=0x3
    if (pmaps[249]) {
        pmap_enter(pmaps[249], 2038349824, 1129013248, 3, 0);
    }
    ops_count++;
    // Op 1784: Enter pmap 256 va=0x3098000 pa=0x49fe4000 prot=0xf
    if (pmaps[256]) {
        pmap_enter(pmaps[256], 50954240, 1241399296, 15, 0);
    }
    ops_count++;
    // Op 1785: Enter pmap 237 va=0x87bec000 pa=0xaee0f000 prot=0x1
    if (pmaps[237]) {
        pmap_enter(pmaps[237], 2277425152, 2933977088, 1, 0);
    }
    ops_count++;
    // Op 1786: Create pmap 266
    pmaps[266] = pmap_create();
    if (!pmaps[266]) kprint("Warning: pmap_create failed for 266\n");
    ops_count++;
    // Op 1787: Enter pmap 245 va=0x6e899000 pa=0x9c58c000 prot=0x5
    if (pmaps[245]) {
        pmap_enter(pmaps[245], 1854509056, 2623062016, 5, 0);
    }
    ops_count++;
    // Op 1788: Enter pmap 229 va=0x1928b000 pa=0x416e3000 prot=0xf
    if (pmaps[229]) {
        pmap_enter(pmaps[229], 422096896, 1097740288, 15, 0);
    }
    ops_count++;
    // Op 1789: Enter pmap 223 va=0xbbfc0000 pa=0xac10f000 prot=0x3
    if (pmaps[223]) {
        pmap_enter(pmaps[223], 3153854464, 2886791168, 3, 0);
    }
    ops_count++;
    // Op 1790: Enter pmap 202 va=0x50328000 pa=0x8c405000 prot=0x1
    if (pmaps[202]) {
        pmap_enter(pmaps[202], 1345486848, 2353025024, 1, 0);
    }
    ops_count++;
    // Op 1791: Remove pmap 155 va=0x460d2000
    if (pmaps[155]) {
        pmap_remove(pmaps[155], 1175265280);
    }
    ops_count++;
    // Op 1792: Enter pmap 174 va=0xaf7cf000 pa=0x72394000 prot=0x5
    if (pmaps[174]) {
        pmap_enter(pmaps[174], 2944200704, 1916354560, 5, 0);
    }
    ops_count++;
    // Op 1793: Destroy pmap 233
    if (pmaps[233]) {
        pmap_destroy(pmaps[233]);
        pmaps[233] = 0;
    }
    ops_count++;
    // Op 1794: Enter pmap 142 va=0x6114000 pa=0x9ea0d000 prot=0x1
    if (pmaps[142]) {
        pmap_enter(pmaps[142], 101793792, 2661339136, 1, 0);
    }
    ops_count++;
    // Op 1795: Create pmap 267
    pmaps[267] = pmap_create();
    if (!pmaps[267]) kprint("Warning: pmap_create failed for 267\n");
    ops_count++;
    // Op 1796: Remove pmap 213 va=0x4f75e000
    if (pmaps[213]) {
        pmap_remove(pmaps[213], 1333125120);
    }
    ops_count++;
    // Op 1797: Create pmap 268
    pmaps[268] = pmap_create();
    if (!pmaps[268]) kprint("Warning: pmap_create failed for 268\n");
    ops_count++;
    // Op 1798: Remove pmap 249 va=0x797ec000
    if (pmaps[249]) {
        pmap_remove(pmaps[249], 2038349824);
    }
    ops_count++;
    // Op 1799: Extract pmap 202 va=0x9d963000
    if (pmaps[202]) {
        pmap_extract(pmaps[202], 2643865600);
    }
    ops_count++;
    kprint(".");
    // Op 1800: Remove pmap 159 va=0x1861d000
    if (pmaps[159]) {
        pmap_remove(pmaps[159], 409063424);
    }
    ops_count++;
    // Op 1801: Extract pmap 232 va=0x40867000
    if (pmaps[232]) {
        pmap_extract(pmaps[232], 1082552320);
    }
    ops_count++;
    // Op 1802: Enter pmap 159 va=0x46fc5000 pa=0xc7006000 prot=0x1
    if (pmaps[159]) {
        pmap_enter(pmaps[159], 1190940672, 3338690560, 1, 0);
    }
    ops_count++;
    // Op 1803: Enter pmap 242 va=0xf601000 pa=0x422be000 prot=0x5
    if (pmaps[242]) {
        pmap_enter(pmaps[242], 257953792, 1110171648, 5, 0);
    }
    ops_count++;
    // Op 1804: Destroy pmap 155
    if (pmaps[155]) {
        pmap_destroy(pmaps[155]);
        pmaps[155] = 0;
    }
    ops_count++;
    // Op 1805: Enter pmap 207 va=0x9c3cc000 pa=0x6493b000 prot=0x3
    if (pmaps[207]) {
        pmap_enter(pmaps[207], 2621227008, 1687400448, 3, 0);
    }
    ops_count++;
    // Op 1806: Remove pmap 175 va=0x24d4e000
    if (pmaps[175]) {
        pmap_remove(pmaps[175], 617930752);
    }
    ops_count++;
    // Op 1807: Enter pmap 258 va=0x37e83000 pa=0x9d3ce000 prot=0x1
    if (pmaps[258]) {
        pmap_enter(pmaps[258], 937963520, 2638012416, 1, 0);
    }
    ops_count++;
    // Op 1808: Extract pmap 229 va=0x1928b000
    if (pmaps[229]) {
        pmap_extract(pmaps[229], 422096896);
    }
    ops_count++;
    // Op 1809: Create pmap 269
    pmaps[269] = pmap_create();
    if (!pmaps[269]) kprint("Warning: pmap_create failed for 269\n");
    ops_count++;
    // Op 1810: Create pmap 270
    pmaps[270] = pmap_create();
    if (!pmaps[270]) kprint("Warning: pmap_create failed for 270\n");
    ops_count++;
    // Op 1811: Enter pmap 115 va=0x9afa000 pa=0xaa339000 prot=0xf
    if (pmaps[115]) {
        pmap_enter(pmaps[115], 162504704, 2855505920, 15, 0);
    }
    ops_count++;
    // Op 1812: Enter pmap 244 va=0x642e4000 pa=0x932b9000 prot=0x1
    if (pmaps[244]) {
        pmap_enter(pmaps[244], 1680752640, 2469105664, 1, 0);
    }
    ops_count++;
    // Op 1813: Enter pmap 265 va=0x7a676000 pa=0x6fb17000 prot=0xf
    if (pmaps[265]) {
        pmap_enter(pmaps[265], 2053595136, 1873899520, 15, 0);
    }
    ops_count++;
    // Op 1814: Protect pmap 159 va=0x15c7c000
    if (pmaps[159]) {
        pmap_protect(pmaps[159], 365412352, 365416448, 1);
    }
    ops_count++;
    // Op 1815: Enter pmap 264 va=0x1f074000 pa=0x9146e000 prot=0x5
    if (pmaps[264]) {
        pmap_enter(pmaps[264], 520568832, 2437341184, 5, 0);
    }
    ops_count++;
    // Op 1816: Enter pmap 237 va=0x668ec000 pa=0xe313000 prot=0x1
    if (pmaps[237]) {
        pmap_enter(pmaps[237], 1720631296, 238104576, 1, 0);
    }
    ops_count++;
    // Op 1817: Enter pmap 245 va=0x1498e000 pa=0xd767a000 prot=0xf
    if (pmaps[245]) {
        pmap_enter(pmaps[245], 345563136, 3613892608, 15, 0);
    }
    ops_count++;
    // Op 1818: Protect pmap 236 va=0xa9009000
    if (pmaps[236]) {
        pmap_protect(pmaps[236], 2835386368, 2835390464, 1);
    }
    ops_count++;
    // Op 1819: Remove pmap 252 va=0xa925e000
    if (pmaps[252]) {
        pmap_remove(pmaps[252], 2837831680);
    }
    ops_count++;
    // Op 1820: Destroy pmap 252
    if (pmaps[252]) {
        pmap_destroy(pmaps[252]);
        pmaps[252] = 0;
    }
    ops_count++;
    // Op 1821: Extract pmap 247 va=0xa1fea000
    if (pmaps[247]) {
        pmap_extract(pmaps[247], 2717818880);
    }
    ops_count++;
    // Op 1822: Enter pmap 206 va=0x23d35000 pa=0x9338000 prot=0x3
    if (pmaps[206]) {
        pmap_enter(pmaps[206], 601051136, 154370048, 3, 0);
    }
    ops_count++;
    // Op 1823: Create pmap 271
    pmaps[271] = pmap_create();
    if (!pmaps[271]) kprint("Warning: pmap_create failed for 271\n");
    ops_count++;
    // Op 1824: Extract pmap 225 va=0x10205000
    if (pmaps[225]) {
        pmap_extract(pmaps[225], 270553088);
    }
    ops_count++;
    // Op 1825: Enter pmap 261 va=0x4340000 pa=0x98ba000 prot=0x5
    if (pmaps[261]) {
        pmap_enter(pmaps[261], 70516736, 160145408, 5, 0);
    }
    ops_count++;
    // Op 1826: Create pmap 272
    pmaps[272] = pmap_create();
    if (!pmaps[272]) kprint("Warning: pmap_create failed for 272\n");
    ops_count++;
    // Op 1827: Enter pmap 217 va=0xa762c000 pa=0xaae53000 prot=0xf
    if (pmaps[217]) {
        pmap_enter(pmaps[217], 2808266752, 2867146752, 15, 0);
    }
    ops_count++;
    // Op 1828: Extract pmap 272 va=0x94501000
    if (pmaps[272]) {
        pmap_extract(pmaps[272], 2488274944);
    }
    ops_count++;
    // Op 1829: Destroy pmap 166
    if (pmaps[166]) {
        pmap_destroy(pmaps[166]);
        pmaps[166] = 0;
    }
    ops_count++;
    // Op 1830: Create pmap 273
    pmaps[273] = pmap_create();
    if (!pmaps[273]) kprint("Warning: pmap_create failed for 273\n");
    ops_count++;
    // Op 1831: Create pmap 274
    pmaps[274] = pmap_create();
    if (!pmaps[274]) kprint("Warning: pmap_create failed for 274\n");
    ops_count++;
    // Op 1832: Remove pmap 265 va=0x7a676000
    if (pmaps[265]) {
        pmap_remove(pmaps[265], 2053595136);
    }
    ops_count++;
    // Op 1833: Destroy pmap 192
    if (pmaps[192]) {
        pmap_destroy(pmaps[192]);
        pmaps[192] = 0;
    }
    ops_count++;
    // Op 1834: Create pmap 275
    pmaps[275] = pmap_create();
    if (!pmaps[275]) kprint("Warning: pmap_create failed for 275\n");
    ops_count++;
    // Op 1835: Remove pmap 242 va=0xf601000
    if (pmaps[242]) {
        pmap_remove(pmaps[242], 257953792);
    }
    ops_count++;
    // Op 1836: Enter pmap 205 va=0x974e9000 pa=0x106f8000 prot=0x1
    if (pmaps[205]) {
        pmap_enter(pmaps[205], 2538508288, 275742720, 1, 0);
    }
    ops_count++;
    // Op 1837: Create pmap 276
    pmaps[276] = pmap_create();
    if (!pmaps[276]) kprint("Warning: pmap_create failed for 276\n");
    ops_count++;
    // Op 1838: Remove pmap 225 va=0x10280000
    if (pmaps[225]) {
        pmap_remove(pmaps[225], 271056896);
    }
    ops_count++;
    // Op 1839: Enter pmap 257 va=0x53011000 pa=0x1efce000 prot=0x3
    if (pmaps[257]) {
        pmap_enter(pmaps[257], 1392578560, 519888896, 3, 0);
    }
    ops_count++;
    // Op 1840: Enter pmap 223 va=0xbab9000 pa=0x97e46000 prot=0x3
    if (pmaps[223]) {
        pmap_enter(pmaps[223], 195792896, 2548326400, 3, 0);
    }
    ops_count++;
    // Op 1841: Enter pmap 186 va=0x461df000 pa=0x24381000 prot=0xf
    if (pmaps[186]) {
        pmap_enter(pmaps[186], 1176367104, 607653888, 15, 0);
    }
    ops_count++;
    // Op 1842: Destroy pmap 253
    if (pmaps[253]) {
        pmap_destroy(pmaps[253]);
        pmaps[253] = 0;
    }
    ops_count++;
    // Op 1843: Remove pmap 263 va=0x3e5f8000
    if (pmaps[263]) {
        pmap_remove(pmaps[263], 1046446080);
    }
    ops_count++;
    // Op 1844: Extract pmap 146 va=0xa1d79000
    if (pmaps[146]) {
        pmap_extract(pmaps[146], 2715258880);
    }
    ops_count++;
    // Op 1845: Remove pmap 202 va=0x71694000
    if (pmaps[202]) {
        pmap_remove(pmaps[202], 1902723072);
    }
    ops_count++;
    // Op 1846: Extract pmap 173 va=0x8a38e000
    if (pmaps[173]) {
        pmap_extract(pmaps[173], 2318983168);
    }
    ops_count++;
    // Op 1847: Extract pmap 237 va=0x668ec000
    if (pmaps[237]) {
        pmap_extract(pmaps[237], 1720631296);
    }
    ops_count++;
    // Op 1848: Remove pmap 219 va=0xc9eb000
    if (pmaps[219]) {
        pmap_remove(pmaps[219], 211726336);
    }
    ops_count++;
    // Op 1849: Remove pmap 273 va=0x734eb000
    if (pmaps[273]) {
        pmap_remove(pmaps[273], 1934536704);
    }
    ops_count++;
    // Op 1850: Enter pmap 206 va=0x9c4c4000 pa=0xb1218000 prot=0x1
    if (pmaps[206]) {
        pmap_enter(pmaps[206], 2622242816, 2971762688, 1, 0);
    }
    ops_count++;
    // Op 1851: Remove pmap 270 va=0x9caf5000
    if (pmaps[270]) {
        pmap_remove(pmaps[270], 2628734976);
    }
    ops_count++;
    // Op 1852: Extract pmap 241 va=0x12b9000
    if (pmaps[241]) {
        pmap_extract(pmaps[241], 19632128);
    }
    ops_count++;
    // Op 1853: Remove pmap 213 va=0x1e150000
    if (pmaps[213]) {
        pmap_remove(pmaps[213], 504692736);
    }
    ops_count++;
    // Op 1854: Extract pmap 175 va=0x8a2b5000
    if (pmaps[175]) {
        pmap_extract(pmaps[175], 2318094336);
    }
    ops_count++;
    // Op 1855: Enter pmap 266 va=0xa0600000 pa=0xa8d9a000 prot=0x3
    if (pmaps[266]) {
        pmap_enter(pmaps[266], 2690646016, 2832834560, 3, 0);
    }
    ops_count++;
    // Op 1856: Enter pmap 213 va=0x1cd89000 pa=0xcbfed000 prot=0x3
    if (pmaps[213]) {
        pmap_enter(pmaps[213], 483954688, 3422474240, 3, 0);
    }
    ops_count++;
    // Op 1857: Remove pmap 267 va=0x7f5d3000
    if (pmaps[267]) {
        pmap_remove(pmaps[267], 2136813568);
    }
    ops_count++;
    // Op 1858: Destroy pmap 146
    if (pmaps[146]) {
        pmap_destroy(pmaps[146]);
        pmaps[146] = 0;
    }
    ops_count++;
    // Op 1859: Create pmap 277
    pmaps[277] = pmap_create();
    if (!pmaps[277]) kprint("Warning: pmap_create failed for 277\n");
    ops_count++;
    // Op 1860: Enter pmap 223 va=0x6f46b000 pa=0x406a8000 prot=0x5
    if (pmaps[223]) {
        pmap_enter(pmaps[223], 1866903552, 1080721408, 5, 0);
    }
    ops_count++;
    // Op 1861: Remove pmap 266 va=0xa0600000
    if (pmaps[266]) {
        pmap_remove(pmaps[266], 2690646016);
    }
    ops_count++;
    // Op 1862: Create pmap 278
    pmaps[278] = pmap_create();
    if (!pmaps[278]) kprint("Warning: pmap_create failed for 278\n");
    ops_count++;
    // Op 1863: Create pmap 279
    pmaps[279] = pmap_create();
    if (!pmaps[279]) kprint("Warning: pmap_create failed for 279\n");
    ops_count++;
    // Op 1864: Enter pmap 205 va=0x770e000 pa=0x7c3cf000 prot=0x1
    if (pmaps[205]) {
        pmap_enter(pmaps[205], 124837888, 2084368384, 1, 0);
    }
    ops_count++;
    // Op 1865: Create pmap 280
    pmaps[280] = pmap_create();
    if (!pmaps[280]) kprint("Warning: pmap_create failed for 280\n");
    ops_count++;
    // Op 1866: Destroy pmap 264
    if (pmaps[264]) {
        pmap_destroy(pmaps[264]);
        pmaps[264] = 0;
    }
    ops_count++;
    // Op 1867: Remove pmap 246 va=0x8910000
    if (pmaps[246]) {
        pmap_remove(pmaps[246], 143720448);
    }
    ops_count++;
    // Op 1868: Destroy pmap 246
    if (pmaps[246]) {
        pmap_destroy(pmaps[246]);
        pmaps[246] = 0;
    }
    ops_count++;
    // Op 1869: Destroy pmap 186
    if (pmaps[186]) {
        pmap_destroy(pmaps[186]);
        pmaps[186] = 0;
    }
    ops_count++;
    // Op 1870: Enter pmap 180 va=0x3cb3d000 pa=0xbced0000 prot=0xf
    if (pmaps[180]) {
        pmap_enter(pmaps[180], 1018417152, 3169648640, 15, 0);
    }
    ops_count++;
    // Op 1871: Extract pmap 258 va=0x12d2e000
    if (pmaps[258]) {
        pmap_extract(pmaps[258], 315809792);
    }
    ops_count++;
    // Op 1872: Enter pmap 273 va=0xde93000 pa=0xe4071000 prot=0xf
    if (pmaps[273]) {
        pmap_enter(pmaps[273], 233385984, 3825668096, 15, 0);
    }
    ops_count++;
    // Op 1873: Create pmap 281
    pmaps[281] = pmap_create();
    if (!pmaps[281]) kprint("Warning: pmap_create failed for 281\n");
    ops_count++;
    // Op 1874: Extract pmap 269 va=0x5f9cc000
    if (pmaps[269]) {
        pmap_extract(pmaps[269], 1604108288);
    }
    ops_count++;
    // Op 1875: Create pmap 282
    pmaps[282] = pmap_create();
    if (!pmaps[282]) kprint("Warning: pmap_create failed for 282\n");
    ops_count++;
    // Op 1876: Extract pmap 223 va=0xbbfc0000
    if (pmaps[223]) {
        pmap_extract(pmaps[223], 3153854464);
    }
    ops_count++;
    // Op 1877: Remove pmap 159 va=0x46fc5000
    if (pmaps[159]) {
        pmap_remove(pmaps[159], 1190940672);
    }
    ops_count++;
    // Op 1878: Create pmap 283
    pmaps[283] = pmap_create();
    if (!pmaps[283]) kprint("Warning: pmap_create failed for 283\n");
    ops_count++;
    // Op 1879: Extract pmap 269 va=0x57f4a000
    if (pmaps[269]) {
        pmap_extract(pmaps[269], 1475649536);
    }
    ops_count++;
    // Op 1880: Destroy pmap 242
    if (pmaps[242]) {
        pmap_destroy(pmaps[242]);
        pmaps[242] = 0;
    }
    ops_count++;
    // Op 1881: Destroy pmap 115
    if (pmaps[115]) {
        pmap_destroy(pmaps[115]);
        pmaps[115] = 0;
    }
    ops_count++;
    // Op 1882: Destroy pmap 125
    if (pmaps[125]) {
        pmap_destroy(pmaps[125]);
        pmaps[125] = 0;
    }
    ops_count++;
    // Op 1883: Destroy pmap 129
    if (pmaps[129]) {
        pmap_destroy(pmaps[129]);
        pmaps[129] = 0;
    }
    ops_count++;
    // Op 1884: Destroy pmap 133
    if (pmaps[133]) {
        pmap_destroy(pmaps[133]);
        pmaps[133] = 0;
    }
    ops_count++;
    // Op 1885: Destroy pmap 142
    if (pmaps[142]) {
        pmap_destroy(pmaps[142]);
        pmaps[142] = 0;
    }
    ops_count++;
    // Op 1886: Destroy pmap 147
    if (pmaps[147]) {
        pmap_destroy(pmaps[147]);
        pmaps[147] = 0;
    }
    ops_count++;
    // Op 1887: Destroy pmap 154
    if (pmaps[154]) {
        pmap_destroy(pmaps[154]);
        pmaps[154] = 0;
    }
    ops_count++;
    // Op 1888: Destroy pmap 159
    if (pmaps[159]) {
        pmap_destroy(pmaps[159]);
        pmaps[159] = 0;
    }
    ops_count++;
    // Op 1889: Destroy pmap 168
    if (pmaps[168]) {
        pmap_destroy(pmaps[168]);
        pmaps[168] = 0;
    }
    ops_count++;
    // Op 1890: Destroy pmap 173
    if (pmaps[173]) {
        pmap_destroy(pmaps[173]);
        pmaps[173] = 0;
    }
    ops_count++;
    // Op 1891: Destroy pmap 174
    if (pmaps[174]) {
        pmap_destroy(pmaps[174]);
        pmaps[174] = 0;
    }
    ops_count++;
    // Op 1892: Destroy pmap 175
    if (pmaps[175]) {
        pmap_destroy(pmaps[175]);
        pmaps[175] = 0;
    }
    ops_count++;
    // Op 1893: Destroy pmap 180
    if (pmaps[180]) {
        pmap_destroy(pmaps[180]);
        pmaps[180] = 0;
    }
    ops_count++;
    // Op 1894: Destroy pmap 184
    if (pmaps[184]) {
        pmap_destroy(pmaps[184]);
        pmaps[184] = 0;
    }
    ops_count++;
    // Op 1895: Destroy pmap 197
    if (pmaps[197]) {
        pmap_destroy(pmaps[197]);
        pmaps[197] = 0;
    }
    ops_count++;
    // Op 1896: Destroy pmap 199
    if (pmaps[199]) {
        pmap_destroy(pmaps[199]);
        pmaps[199] = 0;
    }
    ops_count++;
    // Op 1897: Destroy pmap 200
    if (pmaps[200]) {
        pmap_destroy(pmaps[200]);
        pmaps[200] = 0;
    }
    ops_count++;
    // Op 1898: Destroy pmap 201
    if (pmaps[201]) {
        pmap_destroy(pmaps[201]);
        pmaps[201] = 0;
    }
    ops_count++;
    // Op 1899: Destroy pmap 202
    if (pmaps[202]) {
        pmap_destroy(pmaps[202]);
        pmaps[202] = 0;
    }
    ops_count++;
    kprint(".");
    // Op 1900: Destroy pmap 205
    if (pmaps[205]) {
        pmap_destroy(pmaps[205]);
        pmaps[205] = 0;
    }
    ops_count++;
    // Op 1901: Destroy pmap 206
    if (pmaps[206]) {
        pmap_destroy(pmaps[206]);
        pmaps[206] = 0;
    }
    ops_count++;
    // Op 1902: Destroy pmap 207
    if (pmaps[207]) {
        pmap_destroy(pmaps[207]);
        pmaps[207] = 0;
    }
    ops_count++;
    // Op 1903: Destroy pmap 213
    if (pmaps[213]) {
        pmap_destroy(pmaps[213]);
        pmaps[213] = 0;
    }
    ops_count++;
    // Op 1904: Destroy pmap 217
    if (pmaps[217]) {
        pmap_destroy(pmaps[217]);
        pmaps[217] = 0;
    }
    ops_count++;
    // Op 1905: Destroy pmap 219
    if (pmaps[219]) {
        pmap_destroy(pmaps[219]);
        pmaps[219] = 0;
    }
    ops_count++;
    // Op 1906: Destroy pmap 223
    if (pmaps[223]) {
        pmap_destroy(pmaps[223]);
        pmaps[223] = 0;
    }
    ops_count++;
    // Op 1907: Destroy pmap 225
    if (pmaps[225]) {
        pmap_destroy(pmaps[225]);
        pmaps[225] = 0;
    }
    ops_count++;
    // Op 1908: Destroy pmap 226
    if (pmaps[226]) {
        pmap_destroy(pmaps[226]);
        pmaps[226] = 0;
    }
    ops_count++;
    // Op 1909: Destroy pmap 228
    if (pmaps[228]) {
        pmap_destroy(pmaps[228]);
        pmaps[228] = 0;
    }
    ops_count++;
    // Op 1910: Destroy pmap 229
    if (pmaps[229]) {
        pmap_destroy(pmaps[229]);
        pmaps[229] = 0;
    }
    ops_count++;
    // Op 1911: Destroy pmap 232
    if (pmaps[232]) {
        pmap_destroy(pmaps[232]);
        pmaps[232] = 0;
    }
    ops_count++;
    // Op 1912: Destroy pmap 234
    if (pmaps[234]) {
        pmap_destroy(pmaps[234]);
        pmaps[234] = 0;
    }
    ops_count++;
    // Op 1913: Destroy pmap 235
    if (pmaps[235]) {
        pmap_destroy(pmaps[235]);
        pmaps[235] = 0;
    }
    ops_count++;
    // Op 1914: Destroy pmap 236
    if (pmaps[236]) {
        pmap_destroy(pmaps[236]);
        pmaps[236] = 0;
    }
    ops_count++;
    // Op 1915: Destroy pmap 237
    if (pmaps[237]) {
        pmap_destroy(pmaps[237]);
        pmaps[237] = 0;
    }
    ops_count++;
    // Op 1916: Destroy pmap 238
    if (pmaps[238]) {
        pmap_destroy(pmaps[238]);
        pmaps[238] = 0;
    }
    ops_count++;
    // Op 1917: Destroy pmap 241
    if (pmaps[241]) {
        pmap_destroy(pmaps[241]);
        pmaps[241] = 0;
    }
    ops_count++;
    // Op 1918: Destroy pmap 244
    if (pmaps[244]) {
        pmap_destroy(pmaps[244]);
        pmaps[244] = 0;
    }
    ops_count++;
    // Op 1919: Destroy pmap 245
    if (pmaps[245]) {
        pmap_destroy(pmaps[245]);
        pmaps[245] = 0;
    }
    ops_count++;
    // Op 1920: Destroy pmap 247
    if (pmaps[247]) {
        pmap_destroy(pmaps[247]);
        pmaps[247] = 0;
    }
    ops_count++;
    // Op 1921: Destroy pmap 248
    if (pmaps[248]) {
        pmap_destroy(pmaps[248]);
        pmaps[248] = 0;
    }
    ops_count++;
    // Op 1922: Destroy pmap 249
    if (pmaps[249]) {
        pmap_destroy(pmaps[249]);
        pmaps[249] = 0;
    }
    ops_count++;
    // Op 1923: Destroy pmap 250
    if (pmaps[250]) {
        pmap_destroy(pmaps[250]);
        pmaps[250] = 0;
    }
    ops_count++;
    // Op 1924: Destroy pmap 254
    if (pmaps[254]) {
        pmap_destroy(pmaps[254]);
        pmaps[254] = 0;
    }
    ops_count++;
    // Op 1925: Destroy pmap 255
    if (pmaps[255]) {
        pmap_destroy(pmaps[255]);
        pmaps[255] = 0;
    }
    ops_count++;
    // Op 1926: Destroy pmap 256
    if (pmaps[256]) {
        pmap_destroy(pmaps[256]);
        pmaps[256] = 0;
    }
    ops_count++;
    // Op 1927: Destroy pmap 257
    if (pmaps[257]) {
        pmap_destroy(pmaps[257]);
        pmaps[257] = 0;
    }
    ops_count++;
    // Op 1928: Destroy pmap 258
    if (pmaps[258]) {
        pmap_destroy(pmaps[258]);
        pmaps[258] = 0;
    }
    ops_count++;
    // Op 1929: Destroy pmap 259
    if (pmaps[259]) {
        pmap_destroy(pmaps[259]);
        pmaps[259] = 0;
    }
    ops_count++;
    // Op 1930: Destroy pmap 260
    if (pmaps[260]) {
        pmap_destroy(pmaps[260]);
        pmaps[260] = 0;
    }
    ops_count++;
    // Op 1931: Destroy pmap 261
    if (pmaps[261]) {
        pmap_destroy(pmaps[261]);
        pmaps[261] = 0;
    }
    ops_count++;
    // Op 1932: Destroy pmap 262
    if (pmaps[262]) {
        pmap_destroy(pmaps[262]);
        pmaps[262] = 0;
    }
    ops_count++;
    // Op 1933: Destroy pmap 263
    if (pmaps[263]) {
        pmap_destroy(pmaps[263]);
        pmaps[263] = 0;
    }
    ops_count++;
    // Op 1934: Destroy pmap 265
    if (pmaps[265]) {
        pmap_destroy(pmaps[265]);
        pmaps[265] = 0;
    }
    ops_count++;
    // Op 1935: Destroy pmap 266
    if (pmaps[266]) {
        pmap_destroy(pmaps[266]);
        pmaps[266] = 0;
    }
    ops_count++;
    // Op 1936: Destroy pmap 267
    if (pmaps[267]) {
        pmap_destroy(pmaps[267]);
        pmaps[267] = 0;
    }
    ops_count++;
    // Op 1937: Destroy pmap 268
    if (pmaps[268]) {
        pmap_destroy(pmaps[268]);
        pmaps[268] = 0;
    }
    ops_count++;
    // Op 1938: Destroy pmap 269
    if (pmaps[269]) {
        pmap_destroy(pmaps[269]);
        pmaps[269] = 0;
    }
    ops_count++;
    // Op 1939: Destroy pmap 270
    if (pmaps[270]) {
        pmap_destroy(pmaps[270]);
        pmaps[270] = 0;
    }
    ops_count++;
    // Op 1940: Destroy pmap 271
    if (pmaps[271]) {
        pmap_destroy(pmaps[271]);
        pmaps[271] = 0;
    }
    ops_count++;
    // Op 1941: Destroy pmap 272
    if (pmaps[272]) {
        pmap_destroy(pmaps[272]);
        pmaps[272] = 0;
    }
    ops_count++;
    // Op 1942: Destroy pmap 273
    if (pmaps[273]) {
        pmap_destroy(pmaps[273]);
        pmaps[273] = 0;
    }
    ops_count++;
    // Op 1943: Destroy pmap 274
    if (pmaps[274]) {
        pmap_destroy(pmaps[274]);
        pmaps[274] = 0;
    }
    ops_count++;
    // Op 1944: Destroy pmap 275
    if (pmaps[275]) {
        pmap_destroy(pmaps[275]);
        pmaps[275] = 0;
    }
    ops_count++;
    // Op 1945: Destroy pmap 276
    if (pmaps[276]) {
        pmap_destroy(pmaps[276]);
        pmaps[276] = 0;
    }
    ops_count++;
    // Op 1946: Destroy pmap 277
    if (pmaps[277]) {
        pmap_destroy(pmaps[277]);
        pmaps[277] = 0;
    }
    ops_count++;
    // Op 1947: Destroy pmap 278
    if (pmaps[278]) {
        pmap_destroy(pmaps[278]);
        pmaps[278] = 0;
    }
    ops_count++;
    // Op 1948: Destroy pmap 279
    if (pmaps[279]) {
        pmap_destroy(pmaps[279]);
        pmaps[279] = 0;
    }
    ops_count++;
    // Op 1949: Destroy pmap 280
    if (pmaps[280]) {
        pmap_destroy(pmaps[280]);
        pmaps[280] = 0;
    }
    ops_count++;
    // Op 1950: Destroy pmap 281
    if (pmaps[281]) {
        pmap_destroy(pmaps[281]);
        pmaps[281] = 0;
    }
    ops_count++;
    // Op 1951: Destroy pmap 282
    if (pmaps[282]) {
        pmap_destroy(pmaps[282]);
        pmaps[282] = 0;
    }
    ops_count++;
    // Op 1952: Destroy pmap 283
    if (pmaps[283]) {
        pmap_destroy(pmaps[283]);
        pmaps[283] = 0;
    }
    ops_count++;

    kprint("\nCompleted operations without crash\n");
    kprint("PASS\n");
}