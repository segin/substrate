/*
 * Auto-generated fuzzing test for mmap
 */

#include <vm/vm_area.h>
#include <sys/mman.h>
#include <kern/console.h>

void run_mmap_fuzz_test(void) {
    kprint("\n=== MMAP Fuzzing Test ===\n");
    kprint("Testing random mmap/munmap/mprotect sequences...\n");
    
    void *maps[5000] = {NULL};
    int ops = 0;
    
    // mmap 0
    maps[0] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    kprint(".");
    // mmap 1
    maps[1] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 2
    maps[2] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 0
    if (maps[0]) {
        sys_munmap(maps[0], 4096);
        maps[0] = NULL;
    }
    ops++;
    // munmap 2
    if (maps[2]) {
        sys_munmap(maps[2], 4096);
        maps[2] = NULL;
    }
    ops++;
    // mmap 3
    maps[3] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 1
    if (maps[1]) {
        sys_munmap(maps[1], 4096);
        maps[1] = NULL;
    }
    ops++;
    // munmap 3
    if (maps[3]) {
        sys_munmap(maps[3], 1048576);
        maps[3] = NULL;
    }
    ops++;
    // mmap 4
    maps[4] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 5
    maps[5] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 4
    if (maps[4]) {
        sys_munmap(maps[4], 8192);
        maps[4] = NULL;
    }
    ops++;
    // munmap 5
    if (maps[5]) {
        sys_munmap(maps[5], 4096);
        maps[5] = NULL;
    }
    ops++;
    // mmap 6
    maps[6] = sys_mmap(NULL, 65536, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 6
    if (maps[6]) {
        sys_mprotect(maps[6], 65536, PROT_READ);
    }
    ops++;
    // munmap 6
    if (maps[6]) {
        sys_munmap(maps[6], 65536);
        maps[6] = NULL;
    }
    ops++;
    // mmap 7
    maps[7] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 7
    if (maps[7]) {
        sys_mprotect(maps[7], 16384, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 8
    maps[8] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 9
    maps[9] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 7
    if (maps[7]) {
        sys_munmap(maps[7], 16384);
        maps[7] = NULL;
    }
    ops++;
    // mmap 10
    maps[10] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 8
    if (maps[8]) {
        sys_munmap(maps[8], 16384);
        maps[8] = NULL;
    }
    ops++;
    // mmap 11
    maps[11] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 12
    maps[12] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 12
    if (maps[12]) {
        sys_munmap(maps[12], 8192);
        maps[12] = NULL;
    }
    ops++;
    // mprotect 11
    if (maps[11]) {
        sys_mprotect(maps[11], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 13
    maps[13] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 13
    if (maps[13]) {
        sys_munmap(maps[13], 8192);
        maps[13] = NULL;
    }
    ops++;
    // mprotect 10
    if (maps[10]) {
        sys_mprotect(maps[10], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 14
    maps[14] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 9
    if (maps[9]) {
        sys_munmap(maps[9], 16384);
        maps[9] = NULL;
    }
    ops++;
    // mmap 15
    maps[15] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 16
    maps[16] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 10
    if (maps[10]) {
        sys_mprotect(maps[10], 65536, PROT_READ);
    }
    ops++;
    // munmap 16
    if (maps[16]) {
        sys_munmap(maps[16], 1048576);
        maps[16] = NULL;
    }
    ops++;
    // munmap 14
    if (maps[14]) {
        sys_munmap(maps[14], 8192);
        maps[14] = NULL;
    }
    ops++;
    // mmap 17
    maps[17] = sys_mmap(NULL, 65536, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 18
    maps[18] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 15
    if (maps[15]) {
        sys_mprotect(maps[15], 8192, PROT_READ);
    }
    ops++;
    // mmap 19
    maps[19] = sys_mmap(NULL, 8192, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 20
    maps[20] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 21
    maps[21] = sys_mmap(NULL, 8192, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 11
    if (maps[11]) {
        sys_munmap(maps[11], 4096);
        maps[11] = NULL;
    }
    ops++;
    // mprotect 19
    if (maps[19]) {
        sys_mprotect(maps[19], 8192, PROT_READ);
    }
    ops++;
    // mmap 22
    maps[22] = sys_mmap(NULL, 65536, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 21
    if (maps[21]) {
        sys_munmap(maps[21], 8192);
        maps[21] = NULL;
    }
    ops++;
    // mprotect 19
    if (maps[19]) {
        sys_mprotect(maps[19], 8192, PROT_READ);
    }
    ops++;
    // munmap 18
    if (maps[18]) {
        sys_munmap(maps[18], 16384);
        maps[18] = NULL;
    }
    ops++;
    // mprotect 22
    if (maps[22]) {
        sys_mprotect(maps[22], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 23
    maps[23] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 24
    maps[24] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    kprint(".");
    // munmap 19
    if (maps[19]) {
        sys_munmap(maps[19], 8192);
        maps[19] = NULL;
    }
    ops++;
    // munmap 10
    if (maps[10]) {
        sys_munmap(maps[10], 65536);
        maps[10] = NULL;
    }
    ops++;
    // mmap 25
    maps[25] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 25
    if (maps[25]) {
        sys_mprotect(maps[25], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 26
    maps[26] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 27
    maps[27] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 25
    if (maps[25]) {
        sys_munmap(maps[25], 4096);
        maps[25] = NULL;
    }
    ops++;
    // mmap 28
    maps[28] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 29
    maps[29] = sys_mmap(NULL, 65536, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 30
    maps[30] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 31
    maps[31] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 32
    maps[32] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 17
    if (maps[17]) {
        sys_mprotect(maps[17], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 28
    if (maps[28]) {
        sys_mprotect(maps[28], 4096, PROT_READ);
    }
    ops++;
    // mmap 33
    maps[33] = sys_mmap(NULL, 1048576, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 33
    if (maps[33]) {
        sys_mprotect(maps[33], 1048576, PROT_READ);
    }
    ops++;
    // mmap 34
    maps[34] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 34
    if (maps[34]) {
        sys_mprotect(maps[34], 65536, PROT_READ);
    }
    ops++;
    // mmap 35
    maps[35] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 34
    if (maps[34]) {
        sys_mprotect(maps[34], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 36
    maps[36] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 37
    maps[37] = sys_mmap(NULL, 8192, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 17
    if (maps[17]) {
        sys_munmap(maps[17], 65536);
        maps[17] = NULL;
    }
    ops++;
    // mmap 38
    maps[38] = sys_mmap(NULL, 65536, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 22
    if (maps[22]) {
        sys_mprotect(maps[22], 65536, PROT_READ);
    }
    ops++;
    // mmap 39
    maps[39] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 40
    maps[40] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 34
    if (maps[34]) {
        sys_munmap(maps[34], 65536);
        maps[34] = NULL;
    }
    ops++;
    // munmap 40
    if (maps[40]) {
        sys_munmap(maps[40], 1048576);
        maps[40] = NULL;
    }
    ops++;
    // munmap 29
    if (maps[29]) {
        sys_munmap(maps[29], 65536);
        maps[29] = NULL;
    }
    ops++;
    // mmap 41
    maps[41] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 42
    maps[42] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 22
    if (maps[22]) {
        sys_mprotect(maps[22], 65536, PROT_READ);
    }
    ops++;
    // mmap 43
    maps[43] = sys_mmap(NULL, 1048576, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 39
    if (maps[39]) {
        sys_munmap(maps[39], 4096);
        maps[39] = NULL;
    }
    ops++;
    // mmap 44
    maps[44] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 45
    maps[45] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 45
    if (maps[45]) {
        sys_munmap(maps[45], 8192);
        maps[45] = NULL;
    }
    ops++;
    // munmap 42
    if (maps[42]) {
        sys_munmap(maps[42], 16384);
        maps[42] = NULL;
    }
    ops++;
    // mmap 46
    maps[46] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 47
    maps[47] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 48
    maps[48] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 30
    if (maps[30]) {
        sys_munmap(maps[30], 16384);
        maps[30] = NULL;
    }
    ops++;
    // munmap 31
    if (maps[31]) {
        sys_munmap(maps[31], 1048576);
        maps[31] = NULL;
    }
    ops++;
    // mprotect 20
    if (maps[20]) {
        sys_mprotect(maps[20], 4096, PROT_READ);
    }
    ops++;
    // munmap 32
    if (maps[32]) {
        sys_munmap(maps[32], 16384);
        maps[32] = NULL;
    }
    ops++;
    // mmap 49
    maps[49] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 50
    maps[50] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 51
    maps[51] = sys_mmap(NULL, 8192, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 24
    if (maps[24]) {
        sys_munmap(maps[24], 4096);
        maps[24] = NULL;
    }
    ops++;
    kprint(".");
    // mmap 52
    maps[52] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 20
    if (maps[20]) {
        sys_mprotect(maps[20], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 53
    maps[53] = sys_mmap(NULL, 8192, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 43
    if (maps[43]) {
        sys_munmap(maps[43], 1048576);
        maps[43] = NULL;
    }
    ops++;
    // mprotect 26
    if (maps[26]) {
        sys_mprotect(maps[26], 8192, PROT_READ);
    }
    ops++;
    // mprotect 27
    if (maps[27]) {
        sys_mprotect(maps[27], 8192, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 54
    maps[54] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 55
    maps[55] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 33
    if (maps[33]) {
        sys_mprotect(maps[33], 1048576, PROT_READ);
    }
    ops++;
    // mprotect 46
    if (maps[46]) {
        sys_mprotect(maps[46], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 56
    maps[56] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 41
    if (maps[41]) {
        sys_munmap(maps[41], 16384);
        maps[41] = NULL;
    }
    ops++;
    // mmap 57
    maps[57] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 44
    if (maps[44]) {
        sys_munmap(maps[44], 16384);
        maps[44] = NULL;
    }
    ops++;
    // munmap 51
    if (maps[51]) {
        sys_munmap(maps[51], 8192);
        maps[51] = NULL;
    }
    ops++;
    // mmap 58
    maps[58] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 59
    maps[59] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 38
    if (maps[38]) {
        sys_munmap(maps[38], 65536);
        maps[38] = NULL;
    }
    ops++;
    // munmap 37
    if (maps[37]) {
        sys_munmap(maps[37], 8192);
        maps[37] = NULL;
    }
    ops++;
    // mmap 60
    maps[60] = sys_mmap(NULL, 1048576, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 28
    if (maps[28]) {
        sys_mprotect(maps[28], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 61
    maps[61] = sys_mmap(NULL, 65536, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 62
    maps[62] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 23
    if (maps[23]) {
        sys_munmap(maps[23], 1048576);
        maps[23] = NULL;
    }
    ops++;
    // munmap 46
    if (maps[46]) {
        sys_munmap(maps[46], 4096);
        maps[46] = NULL;
    }
    ops++;
    // munmap 60
    if (maps[60]) {
        sys_munmap(maps[60], 1048576);
        maps[60] = NULL;
    }
    ops++;
    // mmap 63
    maps[63] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 64
    maps[64] = sys_mmap(NULL, 65536, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 65
    maps[65] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 66
    maps[66] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 67
    maps[67] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 47
    if (maps[47]) {
        sys_munmap(maps[47], 4096);
        maps[47] = NULL;
    }
    ops++;
    // munmap 61
    if (maps[61]) {
        sys_munmap(maps[61], 65536);
        maps[61] = NULL;
    }
    ops++;
    // munmap 22
    if (maps[22]) {
        sys_munmap(maps[22], 65536);
        maps[22] = NULL;
    }
    ops++;
    // mprotect 67
    if (maps[67]) {
        sys_mprotect(maps[67], 8192, PROT_READ);
    }
    ops++;
    // munmap 36
    if (maps[36]) {
        sys_munmap(maps[36], 1048576);
        maps[36] = NULL;
    }
    ops++;
    // mprotect 28
    if (maps[28]) {
        sys_mprotect(maps[28], 4096, PROT_READ);
    }
    ops++;
    // mmap 68
    maps[68] = sys_mmap(NULL, 65536, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 69
    maps[69] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 70
    maps[70] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 68
    if (maps[68]) {
        sys_mprotect(maps[68], 65536, PROT_READ);
    }
    ops++;
    // munmap 48
    if (maps[48]) {
        sys_munmap(maps[48], 8192);
        maps[48] = NULL;
    }
    ops++;
    // mmap 71
    maps[71] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 27
    if (maps[27]) {
        sys_munmap(maps[27], 8192);
        maps[27] = NULL;
    }
    ops++;
    // mmap 72
    maps[72] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 69
    if (maps[69]) {
        sys_munmap(maps[69], 1048576);
        maps[69] = NULL;
    }
    ops++;
    // mprotect 57
    if (maps[57]) {
        sys_mprotect(maps[57], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 70
    if (maps[70]) {
        sys_mprotect(maps[70], 8192, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 73
    maps[73] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 52
    if (maps[52]) {
        sys_munmap(maps[52], 4096);
        maps[52] = NULL;
    }
    ops++;
    kprint(".");
    // mmap 74
    maps[74] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 75
    maps[75] = sys_mmap(NULL, 1048576, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 76
    maps[76] = sys_mmap(NULL, 65536, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 77
    maps[77] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 78
    maps[78] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 65
    if (maps[65]) {
        sys_mprotect(maps[65], 4096, PROT_READ);
    }
    ops++;
    // mprotect 72
    if (maps[72]) {
        sys_mprotect(maps[72], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 79
    maps[79] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 74
    if (maps[74]) {
        sys_mprotect(maps[74], 16384, PROT_READ);
    }
    ops++;
    // mmap 80
    maps[80] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 81
    maps[81] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 53
    if (maps[53]) {
        sys_mprotect(maps[53], 8192, PROT_READ);
    }
    ops++;
    // mprotect 20
    if (maps[20]) {
        sys_mprotect(maps[20], 4096, PROT_READ);
    }
    ops++;
    // munmap 53
    if (maps[53]) {
        sys_munmap(maps[53], 8192);
        maps[53] = NULL;
    }
    ops++;
    // mprotect 57
    if (maps[57]) {
        sys_mprotect(maps[57], 4096, PROT_READ);
    }
    ops++;
    // mmap 82
    maps[82] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 83
    maps[83] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 84
    maps[84] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 28
    if (maps[28]) {
        sys_munmap(maps[28], 4096);
        maps[28] = NULL;
    }
    ops++;
    // mprotect 72
    if (maps[72]) {
        sys_mprotect(maps[72], 65536, PROT_READ);
    }
    ops++;
    // munmap 26
    if (maps[26]) {
        sys_munmap(maps[26], 8192);
        maps[26] = NULL;
    }
    ops++;
    // munmap 65
    if (maps[65]) {
        sys_munmap(maps[65], 4096);
        maps[65] = NULL;
    }
    ops++;
    // mmap 85
    maps[85] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 86
    maps[86] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 75
    if (maps[75]) {
        sys_munmap(maps[75], 1048576);
        maps[75] = NULL;
    }
    ops++;
    // mmap 87
    maps[87] = sys_mmap(NULL, 1048576, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 54
    if (maps[54]) {
        sys_mprotect(maps[54], 16384, PROT_READ);
    }
    ops++;
    // mprotect 77
    if (maps[77]) {
        sys_mprotect(maps[77], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 62
    if (maps[62]) {
        sys_mprotect(maps[62], 4096, PROT_READ);
    }
    ops++;
    // munmap 66
    if (maps[66]) {
        sys_munmap(maps[66], 1048576);
        maps[66] = NULL;
    }
    ops++;
    // mmap 88
    maps[88] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 89
    maps[89] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 74
    if (maps[74]) {
        sys_munmap(maps[74], 16384);
        maps[74] = NULL;
    }
    ops++;
    // mmap 90
    maps[90] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 91
    maps[91] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 92
    maps[92] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 93
    maps[93] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 50
    if (maps[50]) {
        sys_mprotect(maps[50], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 67
    if (maps[67]) {
        sys_mprotect(maps[67], 8192, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 79
    if (maps[79]) {
        sys_munmap(maps[79], 16384);
        maps[79] = NULL;
    }
    ops++;
    // mmap 94
    maps[94] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 95
    maps[95] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 96
    maps[96] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 62
    if (maps[62]) {
        sys_munmap(maps[62], 4096);
        maps[62] = NULL;
    }
    ops++;
    // mmap 97
    maps[97] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 88
    if (maps[88]) {
        sys_munmap(maps[88], 16384);
        maps[88] = NULL;
    }
    ops++;
    // mmap 98
    maps[98] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 99
    maps[99] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 100
    maps[100] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 93
    if (maps[93]) {
        sys_mprotect(maps[93], 1048576, PROT_READ | PROT_WRITE);
    }
    ops++;
    kprint(".");
    // mmap 101
    maps[101] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 102
    maps[102] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 103
    maps[103] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 104
    maps[104] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 63
    if (maps[63]) {
        sys_mprotect(maps[63], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 105
    maps[105] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 56
    if (maps[56]) {
        sys_munmap(maps[56], 8192);
        maps[56] = NULL;
    }
    ops++;
    // mmap 106
    maps[106] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 90
    if (maps[90]) {
        sys_munmap(maps[90], 65536);
        maps[90] = NULL;
    }
    ops++;
    // mmap 107
    maps[107] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 108
    maps[108] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 109
    maps[109] = sys_mmap(NULL, 65536, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 110
    maps[110] = sys_mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 111
    maps[111] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 55
    if (maps[55]) {
        sys_mprotect(maps[55], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 110
    if (maps[110]) {
        sys_munmap(maps[110], 4096);
        maps[110] = NULL;
    }
    ops++;
    // munmap 109
    if (maps[109]) {
        sys_munmap(maps[109], 65536);
        maps[109] = NULL;
    }
    ops++;
    // munmap 86
    if (maps[86]) {
        sys_munmap(maps[86], 4096);
        maps[86] = NULL;
    }
    ops++;
    // mprotect 76
    if (maps[76]) {
        sys_mprotect(maps[76], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 112
    maps[112] = sys_mmap(NULL, 1048576, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 67
    if (maps[67]) {
        sys_mprotect(maps[67], 8192, PROT_READ);
    }
    ops++;
    // mmap 113
    maps[113] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 114
    maps[114] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 115
    maps[115] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 116
    maps[116] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 117
    maps[117] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 118
    maps[118] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 115
    if (maps[115]) {
        sys_mprotect(maps[115], 1048576, PROT_READ);
    }
    ops++;
    // mmap 119
    maps[119] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 120
    maps[120] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 93
    if (maps[93]) {
        sys_mprotect(maps[93], 1048576, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 121
    maps[121] = sys_mmap(NULL, 1048576, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 99
    if (maps[99]) {
        sys_munmap(maps[99], 8192);
        maps[99] = NULL;
    }
    ops++;
    // mmap 122
    maps[122] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 105
    if (maps[105]) {
        sys_mprotect(maps[105], 8192, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 112
    if (maps[112]) {
        sys_munmap(maps[112], 1048576);
        maps[112] = NULL;
    }
    ops++;
    // munmap 91
    if (maps[91]) {
        sys_munmap(maps[91], 16384);
        maps[91] = NULL;
    }
    ops++;
    // munmap 87
    if (maps[87]) {
        sys_munmap(maps[87], 1048576);
        maps[87] = NULL;
    }
    ops++;
    // mprotect 63
    if (maps[63]) {
        sys_mprotect(maps[63], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 94
    if (maps[94]) {
        sys_munmap(maps[94], 65536);
        maps[94] = NULL;
    }
    ops++;
    // mprotect 93
    if (maps[93]) {
        sys_mprotect(maps[93], 1048576, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 123
    maps[123] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 124
    maps[124] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 100
    if (maps[100]) {
        sys_mprotect(maps[100], 1048576, PROT_READ);
    }
    ops++;
    // mmap 125
    maps[125] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 126
    maps[126] = sys_mmap(NULL, 65536, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 127
    maps[127] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 116
    if (maps[116]) {
        sys_munmap(maps[116], 16384);
        maps[116] = NULL;
    }
    ops++;
    // mmap 128
    maps[128] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 129
    maps[129] = sys_mmap(NULL, 65536, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    kprint(".");
    // mprotect 103
    if (maps[103]) {
        sys_mprotect(maps[103], 1048576, PROT_READ);
    }
    ops++;
    // munmap 76
    if (maps[76]) {
        sys_munmap(maps[76], 65536);
        maps[76] = NULL;
    }
    ops++;
    // munmap 101
    if (maps[101]) {
        sys_munmap(maps[101], 65536);
        maps[101] = NULL;
    }
    ops++;
    // mmap 130
    maps[130] = sys_mmap(NULL, 8192, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 108
    if (maps[108]) {
        sys_mprotect(maps[108], 16384, PROT_READ);
    }
    ops++;
    // mmap 131
    maps[131] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 95
    if (maps[95]) {
        sys_mprotect(maps[95], 16384, PROT_READ);
    }
    ops++;
    // mmap 132
    maps[132] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 97
    if (maps[97]) {
        sys_mprotect(maps[97], 65536, PROT_READ);
    }
    ops++;
    // mprotect 72
    if (maps[72]) {
        sys_mprotect(maps[72], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 133
    maps[133] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 134
    maps[134] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 135
    maps[135] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 119
    if (maps[119]) {
        sys_munmap(maps[119], 8192);
        maps[119] = NULL;
    }
    ops++;
    // mmap 136
    maps[136] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 93
    if (maps[93]) {
        sys_munmap(maps[93], 1048576);
        maps[93] = NULL;
    }
    ops++;
    // mprotect 125
    if (maps[125]) {
        sys_mprotect(maps[125], 16384, PROT_READ);
    }
    ops++;
    // mmap 137
    maps[137] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 77
    if (maps[77]) {
        sys_mprotect(maps[77], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 71
    if (maps[71]) {
        sys_mprotect(maps[71], 1048576, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 129
    if (maps[129]) {
        sys_mprotect(maps[129], 65536, PROT_READ);
    }
    ops++;
    // munmap 131
    if (maps[131]) {
        sys_munmap(maps[131], 16384);
        maps[131] = NULL;
    }
    ops++;
    // mmap 138
    maps[138] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 139
    maps[139] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 113
    if (maps[113]) {
        sys_munmap(maps[113], 4096);
        maps[113] = NULL;
    }
    ops++;
    // mprotect 122
    if (maps[122]) {
        sys_mprotect(maps[122], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 137
    if (maps[137]) {
        sys_munmap(maps[137], 65536);
        maps[137] = NULL;
    }
    ops++;
    // mprotect 49
    if (maps[49]) {
        sys_mprotect(maps[49], 16384, PROT_READ);
    }
    ops++;
    // mmap 140
    maps[140] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 141
    maps[141] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 142
    maps[142] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 143
    maps[143] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 144
    maps[144] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 82
    if (maps[82]) {
        sys_munmap(maps[82], 16384);
        maps[82] = NULL;
    }
    ops++;
    // mmap 145
    maps[145] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 146
    maps[146] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 147
    maps[147] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 135
    if (maps[135]) {
        sys_munmap(maps[135], 65536);
        maps[135] = NULL;
    }
    ops++;
    // mprotect 147
    if (maps[147]) {
        sys_mprotect(maps[147], 16384, PROT_READ);
    }
    ops++;
    // mmap 148
    maps[148] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 134
    if (maps[134]) {
        sys_munmap(maps[134], 16384);
        maps[134] = NULL;
    }
    ops++;
    // mprotect 85
    if (maps[85]) {
        sys_mprotect(maps[85], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 67
    if (maps[67]) {
        sys_mprotect(maps[67], 8192, PROT_READ);
    }
    ops++;
    // mmap 149
    maps[149] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 150
    maps[150] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 54
    if (maps[54]) {
        sys_munmap(maps[54], 16384);
        maps[54] = NULL;
    }
    ops++;
    // mprotect 143
    if (maps[143]) {
        sys_mprotect(maps[143], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 97
    if (maps[97]) {
        sys_munmap(maps[97], 65536);
        maps[97] = NULL;
    }
    ops++;
    // munmap 122
    if (maps[122]) {
        sys_munmap(maps[122], 4096);
        maps[122] = NULL;
    }
    ops++;
    // mmap 151
    maps[151] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    kprint(".");
    // munmap 50
    if (maps[50]) {
        sys_munmap(maps[50], 65536);
        maps[50] = NULL;
    }
    ops++;
    // mmap 152
    maps[152] = sys_mmap(NULL, 65536, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 67
    if (maps[67]) {
        sys_munmap(maps[67], 8192);
        maps[67] = NULL;
    }
    ops++;
    // mprotect 120
    if (maps[120]) {
        sys_mprotect(maps[120], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 153
    maps[153] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 105
    if (maps[105]) {
        sys_mprotect(maps[105], 8192, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 114
    if (maps[114]) {
        sys_mprotect(maps[114], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 143
    if (maps[143]) {
        sys_mprotect(maps[143], 4096, PROT_READ);
    }
    ops++;
    // mmap 154
    maps[154] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 155
    maps[155] = sys_mmap(NULL, 65536, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 142
    if (maps[142]) {
        sys_munmap(maps[142], 16384);
        maps[142] = NULL;
    }
    ops++;
    // mmap 156
    maps[156] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 157
    maps[157] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 156
    if (maps[156]) {
        sys_munmap(maps[156], 16384);
        maps[156] = NULL;
    }
    ops++;
    // mmap 158
    maps[158] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 144
    if (maps[144]) {
        sys_munmap(maps[144], 8192);
        maps[144] = NULL;
    }
    ops++;
    // mprotect 115
    if (maps[115]) {
        sys_mprotect(maps[115], 1048576, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 159
    maps[159] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 72
    if (maps[72]) {
        sys_mprotect(maps[72], 65536, PROT_READ);
    }
    ops++;
    // mmap 160
    maps[160] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 161
    maps[161] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 70
    if (maps[70]) {
        sys_mprotect(maps[70], 8192, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 108
    if (maps[108]) {
        sys_munmap(maps[108], 16384);
        maps[108] = NULL;
    }
    ops++;
    // mmap 162
    maps[162] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 163
    maps[163] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 128
    if (maps[128]) {
        sys_mprotect(maps[128], 16384, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 164
    maps[164] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 145
    if (maps[145]) {
        sys_mprotect(maps[145], 4096, PROT_READ);
    }
    ops++;
    // munmap 164
    if (maps[164]) {
        sys_munmap(maps[164], 16384);
        maps[164] = NULL;
    }
    ops++;
    // mprotect 57
    if (maps[57]) {
        sys_mprotect(maps[57], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 165
    maps[165] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 158
    if (maps[158]) {
        sys_mprotect(maps[158], 1048576, PROT_READ);
    }
    ops++;
    // munmap 102
    if (maps[102]) {
        sys_munmap(maps[102], 4096);
        maps[102] = NULL;
    }
    ops++;
    // mmap 166
    maps[166] = sys_mmap(NULL, 8192, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 80
    if (maps[80]) {
        sys_mprotect(maps[80], 16384, PROT_READ);
    }
    ops++;
    // munmap 85
    if (maps[85]) {
        sys_munmap(maps[85], 4096);
        maps[85] = NULL;
    }
    ops++;
    // mmap 167
    maps[167] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 168
    maps[168] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 169
    maps[169] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 59
    if (maps[59]) {
        sys_munmap(maps[59], 16384);
        maps[59] = NULL;
    }
    ops++;
    // mmap 170
    maps[170] = sys_mmap(NULL, 16384, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 170
    if (maps[170]) {
        sys_munmap(maps[170], 16384);
        maps[170] = NULL;
    }
    ops++;
    // mmap 171
    maps[171] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 35
    if (maps[35]) {
        sys_munmap(maps[35], 4096);
        maps[35] = NULL;
    }
    ops++;
    // mmap 172
    maps[172] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 153
    if (maps[153]) {
        sys_munmap(maps[153], 8192);
        maps[153] = NULL;
    }
    ops++;
    // munmap 147
    if (maps[147]) {
        sys_munmap(maps[147], 16384);
        maps[147] = NULL;
    }
    ops++;
    // mmap 173
    maps[173] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 174
    maps[174] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 161
    if (maps[161]) {
        sys_mprotect(maps[161], 1048576, PROT_READ | PROT_WRITE);
    }
    ops++;
    kprint(".");
    // mmap 175
    maps[175] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 77
    if (maps[77]) {
        sys_munmap(maps[77], 65536);
        maps[77] = NULL;
    }
    ops++;
    // mprotect 166
    if (maps[166]) {
        sys_mprotect(maps[166], 8192, PROT_READ);
    }
    ops++;
    // mmap 176
    maps[176] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 141
    if (maps[141]) {
        sys_mprotect(maps[141], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 126
    if (maps[126]) {
        sys_munmap(maps[126], 65536);
        maps[126] = NULL;
    }
    ops++;
    // mmap 177
    maps[177] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 155
    if (maps[155]) {
        sys_munmap(maps[155], 65536);
        maps[155] = NULL;
    }
    ops++;
    // mmap 178
    maps[178] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 179
    maps[179] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 169
    if (maps[169]) {
        sys_munmap(maps[169], 4096);
        maps[169] = NULL;
    }
    ops++;
    // munmap 80
    if (maps[80]) {
        sys_munmap(maps[80], 16384);
        maps[80] = NULL;
    }
    ops++;
    // mmap 180
    maps[180] = sys_mmap(NULL, 65536, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 136
    if (maps[136]) {
        sys_munmap(maps[136], 16384);
        maps[136] = NULL;
    }
    ops++;
    // mmap 181
    maps[181] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 107
    if (maps[107]) {
        sys_munmap(maps[107], 4096);
        maps[107] = NULL;
    }
    ops++;
    // mprotect 138
    if (maps[138]) {
        sys_mprotect(maps[138], 8192, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 182
    maps[182] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 182
    if (maps[182]) {
        sys_mprotect(maps[182], 16384, PROT_READ);
    }
    ops++;
    // munmap 121
    if (maps[121]) {
        sys_munmap(maps[121], 1048576);
        maps[121] = NULL;
    }
    ops++;
    // mmap 183
    maps[183] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 184
    maps[184] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 185
    maps[185] = sys_mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 186
    maps[186] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 187
    maps[187] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 188
    maps[188] = sys_mmap(NULL, 1048576, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 189
    maps[189] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 190
    maps[190] = sys_mmap(NULL, 8192, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 191
    maps[191] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 192
    maps[192] = sys_mmap(NULL, 65536, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 193
    maps[193] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 194
    maps[194] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 98
    if (maps[98]) {
        sys_munmap(maps[98], 4096);
        maps[98] = NULL;
    }
    ops++;
    // mmap 195
    maps[195] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 196
    maps[196] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 197
    maps[197] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 195
    if (maps[195]) {
        sys_munmap(maps[195], 1048576);
        maps[195] = NULL;
    }
    ops++;
    // munmap 128
    if (maps[128]) {
        sys_munmap(maps[128], 16384);
        maps[128] = NULL;
    }
    ops++;
    // mmap 198
    maps[198] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 185
    if (maps[185]) {
        sys_mprotect(maps[185], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 145
    if (maps[145]) {
        sys_mprotect(maps[145], 4096, PROT_READ);
    }
    ops++;
    // mmap 199
    maps[199] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 173
    if (maps[173]) {
        sys_munmap(maps[173], 4096);
        maps[173] = NULL;
    }
    ops++;
    // mmap 200
    maps[200] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 201
    maps[201] = sys_mmap(NULL, 8192, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 104
    if (maps[104]) {
        sys_munmap(maps[104], 65536);
        maps[104] = NULL;
    }
    ops++;
    // mprotect 143
    if (maps[143]) {
        sys_mprotect(maps[143], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 190
    if (maps[190]) {
        sys_munmap(maps[190], 8192);
        maps[190] = NULL;
    }
    ops++;
    // mprotect 118
    if (maps[118]) {
        sys_mprotect(maps[118], 1048576, PROT_READ);
    }
    ops++;
    // mmap 202
    maps[202] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    kprint(".");
    // munmap 192
    if (maps[192]) {
        sys_munmap(maps[192], 65536);
        maps[192] = NULL;
    }
    ops++;
    // mmap 203
    maps[203] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 180
    if (maps[180]) {
        sys_munmap(maps[180], 65536);
        maps[180] = NULL;
    }
    ops++;
    // munmap 188
    if (maps[188]) {
        sys_munmap(maps[188], 1048576);
        maps[188] = NULL;
    }
    ops++;
    // mmap 204
    maps[204] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 205
    maps[205] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 145
    if (maps[145]) {
        sys_munmap(maps[145], 4096);
        maps[145] = NULL;
    }
    ops++;
    // munmap 133
    if (maps[133]) {
        sys_munmap(maps[133], 1048576);
        maps[133] = NULL;
    }
    ops++;
    // mmap 206
    maps[206] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 68
    if (maps[68]) {
        sys_mprotect(maps[68], 65536, PROT_READ);
    }
    ops++;
    // munmap 174
    if (maps[174]) {
        sys_munmap(maps[174], 16384);
        maps[174] = NULL;
    }
    ops++;
    // mmap 207
    maps[207] = sys_mmap(NULL, 1048576, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 185
    if (maps[185]) {
        sys_mprotect(maps[185], 4096, PROT_READ);
    }
    ops++;
    // mmap 208
    maps[208] = sys_mmap(NULL, 65536, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 209
    maps[209] = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 210
    maps[210] = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 211
    maps[211] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 202
    if (maps[202]) {
        sys_munmap(maps[202], 8192);
        maps[202] = NULL;
    }
    ops++;
    // munmap 196
    if (maps[196]) {
        sys_munmap(maps[196], 4096);
        maps[196] = NULL;
    }
    ops++;
    // munmap 64
    if (maps[64]) {
        sys_munmap(maps[64], 65536);
        maps[64] = NULL;
    }
    ops++;
    // mmap 212
    maps[212] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 213
    maps[213] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 214
    maps[214] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 208
    if (maps[208]) {
        sys_mprotect(maps[208], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 215
    maps[215] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 143
    if (maps[143]) {
        sys_munmap(maps[143], 4096);
        maps[143] = NULL;
    }
    ops++;
    // mprotect 205
    if (maps[205]) {
        sys_mprotect(maps[205], 65536, PROT_READ);
    }
    ops++;
    // munmap 162
    if (maps[162]) {
        sys_munmap(maps[162], 16384);
        maps[162] = NULL;
    }
    ops++;
    // munmap 33
    if (maps[33]) {
        sys_munmap(maps[33], 1048576);
        maps[33] = NULL;
    }
    ops++;
    // mmap 216
    maps[216] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 177
    if (maps[177]) {
        sys_mprotect(maps[177], 16384, PROT_READ);
    }
    ops++;
    // mmap 217
    maps[217] = sys_mmap(NULL, 8192, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 218
    maps[218] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 200
    if (maps[200]) {
        sys_mprotect(maps[200], 16384, PROT_READ);
    }
    ops++;
    // munmap 96
    if (maps[96]) {
        sys_munmap(maps[96], 1048576);
        maps[96] = NULL;
    }
    ops++;
    // mmap 219
    maps[219] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 220
    maps[220] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 219
    if (maps[219]) {
        sys_munmap(maps[219], 1048576);
        maps[219] = NULL;
    }
    ops++;
    // munmap 171
    if (maps[171]) {
        sys_munmap(maps[171], 1048576);
        maps[171] = NULL;
    }
    ops++;
    // mmap 221
    maps[221] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 114
    if (maps[114]) {
        sys_mprotect(maps[114], 4096, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 194
    if (maps[194]) {
        sys_munmap(maps[194], 4096);
        maps[194] = NULL;
    }
    ops++;
    // mprotect 176
    if (maps[176]) {
        sys_mprotect(maps[176], 8192, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 182
    if (maps[182]) {
        sys_munmap(maps[182], 16384);
        maps[182] = NULL;
    }
    ops++;
    // munmap 103
    if (maps[103]) {
        sys_munmap(maps[103], 1048576);
        maps[103] = NULL;
    }
    ops++;
    // mprotect 204
    if (maps[204]) {
        sys_mprotect(maps[204], 16384, PROT_READ);
    }
    ops++;
    // mprotect 63
    if (maps[63]) {
        sys_mprotect(maps[63], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 193
    if (maps[193]) {
        sys_mprotect(maps[193], 8192, PROT_READ);
    }
    ops++;
    // munmap 186
    if (maps[186]) {
        sys_munmap(maps[186], 65536);
        maps[186] = NULL;
    }
    ops++;
    // mprotect 139
    if (maps[139]) {
        sys_mprotect(maps[139], 16384, PROT_READ);
    }
    ops++;
    kprint(".");
    // munmap 132
    if (maps[132]) {
        sys_munmap(maps[132], 8192);
        maps[132] = NULL;
    }
    ops++;
    // mmap 222
    maps[222] = sys_mmap(NULL, 1048576, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 223
    maps[223] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 167
    if (maps[167]) {
        sys_mprotect(maps[167], 4096, PROT_READ);
    }
    ops++;
    // mmap 224
    maps[224] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 159
    if (maps[159]) {
        sys_mprotect(maps[159], 4096, PROT_READ);
    }
    ops++;
    // mmap 225
    maps[225] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 226
    maps[226] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 179
    if (maps[179]) {
        sys_munmap(maps[179], 16384);
        maps[179] = NULL;
    }
    ops++;
    // mmap 227
    maps[227] = sys_mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 228
    maps[228] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 125
    if (maps[125]) {
        sys_munmap(maps[125], 16384);
        maps[125] = NULL;
    }
    ops++;
    // mmap 229
    maps[229] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 210
    if (maps[210]) {
        sys_munmap(maps[210], 65536);
        maps[210] = NULL;
    }
    ops++;
    // mmap 230
    maps[230] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 204
    if (maps[204]) {
        sys_munmap(maps[204], 16384);
        maps[204] = NULL;
    }
    ops++;
    // mmap 231
    maps[231] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 232
    maps[232] = sys_mmap(NULL, 16384, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 233
    maps[233] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 199
    if (maps[199]) {
        sys_mprotect(maps[199], 4096, PROT_READ);
    }
    ops++;
    // mmap 234
    maps[234] = sys_mmap(NULL, 65536, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 130
    if (maps[130]) {
        sys_mprotect(maps[130], 8192, PROT_READ);
    }
    ops++;
    // mmap 235
    maps[235] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 213
    if (maps[213]) {
        sys_munmap(maps[213], 1048576);
        maps[213] = NULL;
    }
    ops++;
    // mmap 236
    maps[236] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 237
    maps[237] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 222
    if (maps[222]) {
        sys_mprotect(maps[222], 1048576, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mprotect 184
    if (maps[184]) {
        sys_mprotect(maps[184], 8192, PROT_READ);
    }
    ops++;
    // mprotect 70
    if (maps[70]) {
        sys_mprotect(maps[70], 8192, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 238
    maps[238] = sys_mmap(NULL, 8192, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 150
    if (maps[150]) {
        sys_mprotect(maps[150], 65536, PROT_READ | PROT_WRITE);
    }
    ops++;
    // munmap 199
    if (maps[199]) {
        sys_munmap(maps[199], 4096);
        maps[199] = NULL;
    }
    ops++;
    // mmap 239
    maps[239] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 240
    maps[240] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 241
    maps[241] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 163
    if (maps[163]) {
        sys_munmap(maps[163], 8192);
        maps[163] = NULL;
    }
    ops++;
    // mmap 242
    maps[242] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 243
    maps[243] = sys_mmap(NULL, 1048576, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 72
    if (maps[72]) {
        sys_munmap(maps[72], 65536);
        maps[72] = NULL;
    }
    ops++;
    // mmap 244
    maps[244] = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 245
    maps[245] = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 246
    maps[246] = sys_mmap(NULL, 65536, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 247
    maps[247] = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 248
    maps[248] = sys_mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 249
    maps[249] = sys_mmap(NULL, 8192, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mprotect 224
    if (maps[224]) {
        sys_mprotect(maps[224], 16384, PROT_READ | PROT_WRITE);
    }
    ops++;
    // mmap 250
    maps[250] = sys_mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ops++;
    // mmap 251
    maps[251] = sys_mmap(NULL, 1048576, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ops++;
    // munmap 161
    if (maps[161]) {
        sys_munmap(maps[161], 1048576);
        maps[161] = NULL;
    }
    ops++;
    // munmap 15
    if (maps[15]) {
        sys_munmap(maps[15], 8192);
        maps[15] = NULL;
    }
    ops++;
    kprint(".");
    // munmap 20
    if (maps[20]) {
        sys_munmap(maps[20], 4096);
        maps[20] = NULL;
    }
    ops++;
    // munmap 49
    if (maps[49]) {
        sys_munmap(maps[49], 16384);
        maps[49] = NULL;
    }
    ops++;
    // munmap 55
    if (maps[55]) {
        sys_munmap(maps[55], 4096);
        maps[55] = NULL;
    }
    ops++;
    // munmap 57
    if (maps[57]) {
        sys_munmap(maps[57], 4096);
        maps[57] = NULL;
    }
    ops++;
    // munmap 58
    if (maps[58]) {
        sys_munmap(maps[58], 4096);
        maps[58] = NULL;
    }
    ops++;
    // munmap 63
    if (maps[63]) {
        sys_munmap(maps[63], 65536);
        maps[63] = NULL;
    }
    ops++;
    // munmap 68
    if (maps[68]) {
        sys_munmap(maps[68], 65536);
        maps[68] = NULL;
    }
    ops++;
    // munmap 70
    if (maps[70]) {
        sys_munmap(maps[70], 8192);
        maps[70] = NULL;
    }
    ops++;
    // munmap 71
    if (maps[71]) {
        sys_munmap(maps[71], 1048576);
        maps[71] = NULL;
    }
    ops++;
    // munmap 73
    if (maps[73]) {
        sys_munmap(maps[73], 8192);
        maps[73] = NULL;
    }
    ops++;
    // munmap 78
    if (maps[78]) {
        sys_munmap(maps[78], 8192);
        maps[78] = NULL;
    }
    ops++;
    // munmap 81
    if (maps[81]) {
        sys_munmap(maps[81], 16384);
        maps[81] = NULL;
    }
    ops++;
    // munmap 83
    if (maps[83]) {
        sys_munmap(maps[83], 65536);
        maps[83] = NULL;
    }
    ops++;
    // munmap 84
    if (maps[84]) {
        sys_munmap(maps[84], 4096);
        maps[84] = NULL;
    }
    ops++;
    // munmap 89
    if (maps[89]) {
        sys_munmap(maps[89], 65536);
        maps[89] = NULL;
    }
    ops++;
    // munmap 92
    if (maps[92]) {
        sys_munmap(maps[92], 1048576);
        maps[92] = NULL;
    }
    ops++;
    // munmap 95
    if (maps[95]) {
        sys_munmap(maps[95], 16384);
        maps[95] = NULL;
    }
    ops++;
    // munmap 100
    if (maps[100]) {
        sys_munmap(maps[100], 1048576);
        maps[100] = NULL;
    }
    ops++;
    // munmap 105
    if (maps[105]) {
        sys_munmap(maps[105], 8192);
        maps[105] = NULL;
    }
    ops++;
    // munmap 106
    if (maps[106]) {
        sys_munmap(maps[106], 4096);
        maps[106] = NULL;
    }
    ops++;
    // munmap 111
    if (maps[111]) {
        sys_munmap(maps[111], 1048576);
        maps[111] = NULL;
    }
    ops++;
    // munmap 114
    if (maps[114]) {
        sys_munmap(maps[114], 4096);
        maps[114] = NULL;
    }
    ops++;
    // munmap 115
    if (maps[115]) {
        sys_munmap(maps[115], 1048576);
        maps[115] = NULL;
    }
    ops++;
    // munmap 117
    if (maps[117]) {
        sys_munmap(maps[117], 8192);
        maps[117] = NULL;
    }
    ops++;
    // munmap 118
    if (maps[118]) {
        sys_munmap(maps[118], 1048576);
        maps[118] = NULL;
    }
    ops++;
    // munmap 120
    if (maps[120]) {
        sys_munmap(maps[120], 65536);
        maps[120] = NULL;
    }
    ops++;
    // munmap 123
    if (maps[123]) {
        sys_munmap(maps[123], 16384);
        maps[123] = NULL;
    }
    ops++;
    // munmap 124
    if (maps[124]) {
        sys_munmap(maps[124], 65536);
        maps[124] = NULL;
    }
    ops++;
    // munmap 127
    if (maps[127]) {
        sys_munmap(maps[127], 8192);
        maps[127] = NULL;
    }
    ops++;
    // munmap 129
    if (maps[129]) {
        sys_munmap(maps[129], 65536);
        maps[129] = NULL;
    }
    ops++;
    // munmap 130
    if (maps[130]) {
        sys_munmap(maps[130], 8192);
        maps[130] = NULL;
    }
    ops++;
    // munmap 138
    if (maps[138]) {
        sys_munmap(maps[138], 8192);
        maps[138] = NULL;
    }
    ops++;
    // munmap 139
    if (maps[139]) {
        sys_munmap(maps[139], 16384);
        maps[139] = NULL;
    }
    ops++;
    // munmap 140
    if (maps[140]) {
        sys_munmap(maps[140], 16384);
        maps[140] = NULL;
    }
    ops++;
    // munmap 141
    if (maps[141]) {
        sys_munmap(maps[141], 4096);
        maps[141] = NULL;
    }
    ops++;
    // munmap 146
    if (maps[146]) {
        sys_munmap(maps[146], 1048576);
        maps[146] = NULL;
    }
    ops++;
    // munmap 148
    if (maps[148]) {
        sys_munmap(maps[148], 16384);
        maps[148] = NULL;
    }
    ops++;
    // munmap 149
    if (maps[149]) {
        sys_munmap(maps[149], 16384);
        maps[149] = NULL;
    }
    ops++;
    // munmap 150
    if (maps[150]) {
        sys_munmap(maps[150], 65536);
        maps[150] = NULL;
    }
    ops++;
    // munmap 151
    if (maps[151]) {
        sys_munmap(maps[151], 8192);
        maps[151] = NULL;
    }
    ops++;
    // munmap 152
    if (maps[152]) {
        sys_munmap(maps[152], 65536);
        maps[152] = NULL;
    }
    ops++;
    // munmap 154
    if (maps[154]) {
        sys_munmap(maps[154], 8192);
        maps[154] = NULL;
    }
    ops++;
    // munmap 157
    if (maps[157]) {
        sys_munmap(maps[157], 8192);
        maps[157] = NULL;
    }
    ops++;
    // munmap 158
    if (maps[158]) {
        sys_munmap(maps[158], 1048576);
        maps[158] = NULL;
    }
    ops++;
    // munmap 159
    if (maps[159]) {
        sys_munmap(maps[159], 4096);
        maps[159] = NULL;
    }
    ops++;
    // munmap 160
    if (maps[160]) {
        sys_munmap(maps[160], 1048576);
        maps[160] = NULL;
    }
    ops++;
    // munmap 165
    if (maps[165]) {
        sys_munmap(maps[165], 8192);
        maps[165] = NULL;
    }
    ops++;
    // munmap 166
    if (maps[166]) {
        sys_munmap(maps[166], 8192);
        maps[166] = NULL;
    }
    ops++;
    // munmap 167
    if (maps[167]) {
        sys_munmap(maps[167], 4096);
        maps[167] = NULL;
    }
    ops++;
    // munmap 168
    if (maps[168]) {
        sys_munmap(maps[168], 8192);
        maps[168] = NULL;
    }
    ops++;
    kprint(".");
    // munmap 172
    if (maps[172]) {
        sys_munmap(maps[172], 65536);
        maps[172] = NULL;
    }
    ops++;
    // munmap 175
    if (maps[175]) {
        sys_munmap(maps[175], 1048576);
        maps[175] = NULL;
    }
    ops++;
    // munmap 176
    if (maps[176]) {
        sys_munmap(maps[176], 8192);
        maps[176] = NULL;
    }
    ops++;
    // munmap 177
    if (maps[177]) {
        sys_munmap(maps[177], 16384);
        maps[177] = NULL;
    }
    ops++;
    // munmap 178
    if (maps[178]) {
        sys_munmap(maps[178], 4096);
        maps[178] = NULL;
    }
    ops++;
    // munmap 181
    if (maps[181]) {
        sys_munmap(maps[181], 1048576);
        maps[181] = NULL;
    }
    ops++;
    // munmap 183
    if (maps[183]) {
        sys_munmap(maps[183], 16384);
        maps[183] = NULL;
    }
    ops++;
    // munmap 184
    if (maps[184]) {
        sys_munmap(maps[184], 8192);
        maps[184] = NULL;
    }
    ops++;
    // munmap 185
    if (maps[185]) {
        sys_munmap(maps[185], 4096);
        maps[185] = NULL;
    }
    ops++;
    // munmap 187
    if (maps[187]) {
        sys_munmap(maps[187], 16384);
        maps[187] = NULL;
    }
    ops++;
    // munmap 189
    if (maps[189]) {
        sys_munmap(maps[189], 65536);
        maps[189] = NULL;
    }
    ops++;
    // munmap 191
    if (maps[191]) {
        sys_munmap(maps[191], 16384);
        maps[191] = NULL;
    }
    ops++;
    // munmap 193
    if (maps[193]) {
        sys_munmap(maps[193], 8192);
        maps[193] = NULL;
    }
    ops++;
    // munmap 197
    if (maps[197]) {
        sys_munmap(maps[197], 4096);
        maps[197] = NULL;
    }
    ops++;
    // munmap 198
    if (maps[198]) {
        sys_munmap(maps[198], 4096);
        maps[198] = NULL;
    }
    ops++;
    // munmap 200
    if (maps[200]) {
        sys_munmap(maps[200], 16384);
        maps[200] = NULL;
    }
    ops++;
    // munmap 201
    if (maps[201]) {
        sys_munmap(maps[201], 8192);
        maps[201] = NULL;
    }
    ops++;
    // munmap 203
    if (maps[203]) {
        sys_munmap(maps[203], 65536);
        maps[203] = NULL;
    }
    ops++;
    // munmap 205
    if (maps[205]) {
        sys_munmap(maps[205], 65536);
        maps[205] = NULL;
    }
    ops++;
    // munmap 206
    if (maps[206]) {
        sys_munmap(maps[206], 4096);
        maps[206] = NULL;
    }
    ops++;
    // munmap 207
    if (maps[207]) {
        sys_munmap(maps[207], 1048576);
        maps[207] = NULL;
    }
    ops++;
    // munmap 208
    if (maps[208]) {
        sys_munmap(maps[208], 65536);
        maps[208] = NULL;
    }
    ops++;
    // munmap 209
    if (maps[209]) {
        sys_munmap(maps[209], 16384);
        maps[209] = NULL;
    }
    ops++;
    // munmap 211
    if (maps[211]) {
        sys_munmap(maps[211], 16384);
        maps[211] = NULL;
    }
    ops++;
    // munmap 212
    if (maps[212]) {
        sys_munmap(maps[212], 1048576);
        maps[212] = NULL;
    }
    ops++;
    // munmap 214
    if (maps[214]) {
        sys_munmap(maps[214], 8192);
        maps[214] = NULL;
    }
    ops++;
    // munmap 215
    if (maps[215]) {
        sys_munmap(maps[215], 4096);
        maps[215] = NULL;
    }
    ops++;
    // munmap 216
    if (maps[216]) {
        sys_munmap(maps[216], 8192);
        maps[216] = NULL;
    }
    ops++;
    // munmap 217
    if (maps[217]) {
        sys_munmap(maps[217], 8192);
        maps[217] = NULL;
    }
    ops++;
    // munmap 218
    if (maps[218]) {
        sys_munmap(maps[218], 4096);
        maps[218] = NULL;
    }
    ops++;
    // munmap 220
    if (maps[220]) {
        sys_munmap(maps[220], 16384);
        maps[220] = NULL;
    }
    ops++;
    // munmap 221
    if (maps[221]) {
        sys_munmap(maps[221], 8192);
        maps[221] = NULL;
    }
    ops++;
    // munmap 222
    if (maps[222]) {
        sys_munmap(maps[222], 1048576);
        maps[222] = NULL;
    }
    ops++;
    // munmap 223
    if (maps[223]) {
        sys_munmap(maps[223], 8192);
        maps[223] = NULL;
    }
    ops++;
    // munmap 224
    if (maps[224]) {
        sys_munmap(maps[224], 16384);
        maps[224] = NULL;
    }
    ops++;
    // munmap 225
    if (maps[225]) {
        sys_munmap(maps[225], 4096);
        maps[225] = NULL;
    }
    ops++;
    // munmap 226
    if (maps[226]) {
        sys_munmap(maps[226], 16384);
        maps[226] = NULL;
    }
    ops++;
    // munmap 227
    if (maps[227]) {
        sys_munmap(maps[227], 4096);
        maps[227] = NULL;
    }
    ops++;
    // munmap 228
    if (maps[228]) {
        sys_munmap(maps[228], 8192);
        maps[228] = NULL;
    }
    ops++;
    // munmap 229
    if (maps[229]) {
        sys_munmap(maps[229], 4096);
        maps[229] = NULL;
    }
    ops++;
    // munmap 230
    if (maps[230]) {
        sys_munmap(maps[230], 4096);
        maps[230] = NULL;
    }
    ops++;
    // munmap 231
    if (maps[231]) {
        sys_munmap(maps[231], 8192);
        maps[231] = NULL;
    }
    ops++;
    // munmap 232
    if (maps[232]) {
        sys_munmap(maps[232], 16384);
        maps[232] = NULL;
    }
    ops++;
    // munmap 233
    if (maps[233]) {
        sys_munmap(maps[233], 4096);
        maps[233] = NULL;
    }
    ops++;
    // munmap 234
    if (maps[234]) {
        sys_munmap(maps[234], 65536);
        maps[234] = NULL;
    }
    ops++;
    // munmap 235
    if (maps[235]) {
        sys_munmap(maps[235], 4096);
        maps[235] = NULL;
    }
    ops++;
    // munmap 236
    if (maps[236]) {
        sys_munmap(maps[236], 1048576);
        maps[236] = NULL;
    }
    ops++;
    // munmap 237
    if (maps[237]) {
        sys_munmap(maps[237], 1048576);
        maps[237] = NULL;
    }
    ops++;
    // munmap 238
    if (maps[238]) {
        sys_munmap(maps[238], 8192);
        maps[238] = NULL;
    }
    ops++;
    // munmap 239
    if (maps[239]) {
        sys_munmap(maps[239], 4096);
        maps[239] = NULL;
    }
    ops++;
    kprint(".");
    // munmap 240
    if (maps[240]) {
        sys_munmap(maps[240], 4096);
        maps[240] = NULL;
    }
    ops++;
    // munmap 241
    if (maps[241]) {
        sys_munmap(maps[241], 16384);
        maps[241] = NULL;
    }
    ops++;
    // munmap 242
    if (maps[242]) {
        sys_munmap(maps[242], 8192);
        maps[242] = NULL;
    }
    ops++;
    // munmap 243
    if (maps[243]) {
        sys_munmap(maps[243], 1048576);
        maps[243] = NULL;
    }
    ops++;
    // munmap 244
    if (maps[244]) {
        sys_munmap(maps[244], 8192);
        maps[244] = NULL;
    }
    ops++;
    // munmap 245
    if (maps[245]) {
        sys_munmap(maps[245], 16384);
        maps[245] = NULL;
    }
    ops++;
    // munmap 246
    if (maps[246]) {
        sys_munmap(maps[246], 65536);
        maps[246] = NULL;
    }
    ops++;
    // munmap 247
    if (maps[247]) {
        sys_munmap(maps[247], 4096);
        maps[247] = NULL;
    }
    ops++;
    // munmap 248
    if (maps[248]) {
        sys_munmap(maps[248], 4096);
        maps[248] = NULL;
    }
    ops++;
    // munmap 249
    if (maps[249]) {
        sys_munmap(maps[249], 8192);
        maps[249] = NULL;
    }
    ops++;
    // munmap 250
    if (maps[250]) {
        sys_munmap(maps[250], 4096);
        maps[250] = NULL;
    }
    ops++;
    // munmap 251
    if (maps[251]) {
        sys_munmap(maps[251], 1048576);
        maps[251] = NULL;
    }
    ops++;

    kprint("\nCompleted ");
    kprint(" operations without crash\n");
    kprint("PASS\n");
}
