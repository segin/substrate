# Substrate Native ABI (AMD64)

This document describes the binary interface for the Substrate Native personality on x86-64 (AMD64).

## 1. System Calls

### Mechanism
*   **Instruction**: `syscall`
*   **System Call Number**: Passed in `RAX` register.
*   **Arguments**: Passed in registers (System V AMD64 Convention).
*   **Return Value**: Returned in `RAX` register.
    *   **Success**: Non-negative value (or specific valid negative pointers).
    *   **Error**: Negative value in the range `[-4095, -1]`. The absolute value corresponds to the `errno` code.
    *   **libc Responsibility**: The C library wrapper is responsible for checking this range, negating the value to set `errno`, and returning `-1`.

### Argument Passing
Substrate Native on AMD64 follows the **System V AMD64 ABI** for system calls.

| Argument | Register |
| :---     | :---     |
| Arg 1    | `RDI`    |
| Arg 2    | `RSI`    |
| Arg 3    | `RDX`    |
| Arg 4    | `R10` (not RCX) |
| Arg 5    | `R8`     |
| Arg 6    | `R9`     |

*   **Note**: The kernel clobbers `RCX` and `R11` (used by `syscall` instruction for IP and Flags).
*   Stack is typically **not** used for arguments unless there are > 6 (rare).

### Syscall Table
*Same numbering as i386.*

| Number | Name          | Description                                 |
| :---   | :---          | :---                                        |
| 1      | `exit`        | Terminate process                           |
| 2      | `fork`        | Create new process                          |
| 3      | `read`        | Read from file descriptor                   |
| 4      | `write`       | Write to file descriptor                    |
| 5      | `open`        | Open file                                   |
| 6      | `close`       | Close file descriptor                       |
| 9      | `link`        | Create hard link                            |
| 10     | `unlink`      | Remove directory entry                      |
| 11     | `execve`      | Execute program                             |
| 12     | `chdir`       | Change working directory                    |
| 13     | `time`        | Get time (seconds)                          |
| 19     | `lseek`       | Reposition read/write offset                |
| 20     | `getpid`      | Get process ID                              |
| 21     | `mount`       | Mount filesystem                            |
| 22     | `umount`      | Unmount filesystem                          |
| 23     | `setuid`      | Set user ID                                 |
| 24     | `getuid`      | Get user ID                                 |
| 33     | `access`      | Check file permissions                      |
| 36     | `sync`        | Commit buffer cache to disk                 |
| 37     | `kill`        | Send signal to process                      |
| 39     | `mkdir`       | Create directory                            |
| 40     | `rmdir`       | Remove directory                            |
| 42     | `pipe`        | Create pipe                                 |
| 46     | `setgid`      | Set group ID                                |
| 47     | `getgid`      | Get group ID                                |
| 48     | `signal`      | Install signal handler (Action)             |
| 49     | `geteuid`     | Get effective user ID                       |
| 50     | `getegid`     | Get effective group ID                      |
| 51     | `acct`        | Process accounting                          |
| 54     | `ioctl`       | I/O Control                                 |
| 63     | `dup2`        | Duplicate file descriptor to specific index |
| 85     | `readlink`    | Read value of a symbolic link               |
| 106    | `stat`        | Get file status (64-bit)                    |
| 107    | `lstat`       | Get link status (64-bit)                    |
| 108    | `fstat`       | Get file status by FD (64-bit)              |
| 119    | `sigreturn`   | Return from signal handler                  |
| 122    | `uname`       | Get system name/info                        |
| 141    | `getdents`    | Get directory entries                       |
| 144    | `msync`       | Synchronize memory with file                |
| 162    | `nanosleep`   | High-resolution sleep                       |
| 183    | `getcwd`      | Get current working directory               |
| 186    | `sigaltstack` | Set/get alternate signal stack              |
| 209    | `poll`        | Wait for events on FDs                      |
| 241    | `pmap_stats`  | Get memory map statistics                   |
| 455    | `thr_new`     | Create new thread                           |

## 2. Data Structures

### File Metadata (`struct stat`)
Defined in `<sys/stat.h>`.
*   64-bit types are native.
*   Off_t is 64-bit.
*   Pointers (if any) implicit in padding or unused.

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

**(Total Size: 92 bytes - note: alignment on x64 may induce gaps between fields if not packed, but this struct uses fixed width types that align naturally to 4/8 bytes as laid out above.)**

### System Info (`struct utsname`)
Buffers are 256 bytes.

### Directory Entry (`struct dirent`)
| Field      | Type             | Size | Description                                  |
| :---       | :---             | :--- | :---                                         |
| `d_ino`    | `uint64_t`       | 8    | Inode number                                 |
| `d_off`    | `uint64_t`       | 8    | Offset to next entry                         |
| `d_reclen` | `unsigned short` | 2    | Length of this record                        |
| `d_name`   | `char[]`         | Var  | Filename (null-terminated)                   |

### Thread Creation (`struct thr_param`)
**Pointers are 8 bytes.**

| Field        | Type        | Size | Description                      |
| :---         | :---        | :--- | :---                             |
| `start_func` | `ptr`       | 8    | Entry point function             |
| `arg`        | `ptr`       | 8    | Argument for entry point         |
| `stack_base` | `ptr`       | 8    | Stack base address               |
| `stack_size` | `size_t`    | 8    | Stack size in bytes              |
| `tls_base`   | `ptr`       | 8    | TLS segment base address         |
| `tls_size`   | `size_t`    | 8    | TLS segment size                 |
| `child_tid`  | `long*`     | 8    | Address to write Child TID       |
| `parent_tid` | `long*`     | 8    | Address to write Parent TID      |
| `flags`      | `int`       | 4    | Creation flags                   |
*(Padding may exist after flags to align struct size)*

### Signal Stack (`stack_t`)
| Field      | Type     | Size | Description                 |
| :---       | :---     | :--- | :---                        |
| `ss_sp`    | `void*`  | 8    | Stack base/pointer          |
| `ss_flags` | `int`    | 4    | Flags (`__SS_DISABLE__`...) |
| `ss_size`  | `size_t` | 8    | Stack size                  |
*(Note: struct alignment is 8 bytes)*

---

## 3. Process Initialization

### Stack Layout (Top to Bottom)
**Pointers are 8 bytes (64-bit).**

| Content             | Description                                                   |
| :---                | :---                                                          |
| Strings             | String data.                                                  |
| `AT_NULL`           | `{0, 0}` (16 bytes)                                           |
| Auxv Entries        | `{Type, Value}` (16 bytes each)                               |
| `AT_RANDOM` data    | 16 bytes.                                                     |
| `Envp[]`            | 8-byte pointers to environment strings, `NULL` terminated.    |
| `Argv[]`            | 8-byte pointers to argument strings, `NULL` terminated.       |
| `Argc`              | 8-byte Integer argument count (or 4-byte passed in RDI usually, but stack layout standard holds). |
| **Stack Pointer**   | `RSP` points here at entry.                                   |

*Note: In AMD64 System V ABI, the entry point arguments are actually: `RSP` points to `argc`. `RSP+8` points to `argv[0]`. `RSP` must be 16-byte aligned before `call` instruction, but at process entry boundaries it follows the setup above.*

## 4. Signal Handling

### Signal Frame
*   Platform dependent, but generally `ucontext_t` heavy.
*   **Instruction Pointer**: `RIP`
*   **Stack Pointer**: `RSP`
*   Registers saved as 64-bit values.
