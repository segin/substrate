#ifndef FBS_USER_H
#define FBS_USER_H
struct freebsd_stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    struct { int32_t tv_sec; int32_t tv_nsec; } st_atim;
    struct { int32_t tv_sec; int32_t tv_nsec; } st_mtim;
    struct { int32_t tv_sec; int32_t tv_nsec; } st_ctim;
    uint64_t st_size;
    uint64_t st_blocks;
    uint32_t st_blksize;
};
struct freebsd11_stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    struct { int32_t tv_sec; int32_t tv_nsec; } st_atim;
    struct { int32_t tv_sec; int32_t tv_nsec; } st_mtim;
    struct { int32_t tv_sec; int32_t tv_nsec; } st_ctim;
    uint64_t st_size;
    uint64_t st_blocks;
    uint32_t st_blksize;
};
#endif
