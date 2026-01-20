# Substrate Native ABI

This document describes the binary interface for the Substrate Native personality.

## File Metadata (`struct stat`)

The native ABI enforces a single, 64-bit compatible `struct stat` for all file metadata operations. There are no legacy 32-bit `stat` system calls in the native personality.

### Structure Layout

Defined in `<sys/stat.h>`. All types are little-endian.

| Offset | Field           | Type       | Size | Description                         |
| :---   | :---            | :---       | :--- | :---                                |
| 0      | `st_dev`        | `uint32_t` | 4    | Device ID of device containing file |
| 4      | `st_ino`        | `uint32_t` | 4    | Inode number                        |
| 8      | `st_mode`       | `uint16_t` | 2    | File type and mode                  |
| 10     | `st_nlink`      | `uint16_t` | 2    | Number of hard links                |
| 12     | `st_uid`        | `uint16_t` | 2    | User ID of owner                    |
| 14     | `st_gid`        | `uint16_t` | 2    | Group ID of owner                   |
| 16     | `st_rdev`       | `uint32_t` | 4    | Device ID (if special file)         |
| 20     | `st_size`       | `off_t`    | 8    | Total size, in bytes                |
| 28     | `st_blksize`    | `uint32_t` | 4    | Block size for filesystem I/O       |
| 32     | `st_pad1`       | `uint32_t` | 4    | Padding                             |
| 36     | `st_blocks`     | `blkcnt_t` | 8    | Number of 512B blocks allocated     |
| 44     | `st_atime`      | `time_t`   | 8    | Time of last access                 |
| 52     | `st_atime_nsec` | `uint32_t` | 4    | Nsecs of last access                |
| 56     | `st_pad2`       | `uint32_t` | 4    | Padding                             |
| 60     | `st_mtime`      | `time_t`   | 8    | Time of last modification           |
| 68     | `st_mtime_nsec` | `uint32_t` | 4    | Nsecs of last modification          |
| 72     | `st_pad3`       | `uint32_t` | 4    | Padding                             |
| 76     | `st_ctime`      | `time_t`   | 8    | Time of last status change          |
| 84     | `st_ctime_nsec` | `uint32_t` | 4    | Nsecs of last status change         |
| 88     | `st_pad4`       | `uint32_t` | 4    | Padding                             |

Total Size: 92 bytes.

### Usage

*   Native binaries must link against the updated `libc` which uses this structure.
*   System calls `SYS_STAT`, `SYS_FSTAT`, `SYS_LSTAT` expect a pointer to this 92-byte structure.
*   Legacy compatibility for imported binaries (Linux/FreeBSD) is handled via personality translation layers, not by exposing legacy syscalls in the native table.
