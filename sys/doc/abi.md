# Substrate Native ABI

This document describes the binary interface for the Substrate Native personality.

## 1. System Calls

### Mechanism
*   **Interrupt**: `0x80`
*   **System Call Number**: Passed in `EAX` register.
*   **Arguments**: Passed on the stack (see Argument Passing).
*   **Return Value**: Returned in `EAX` register. 
    *   **Success**: Non-negative value (or specific valid negative pointers).
    *   **Error**: Negative value in the range `[-4095, -1]`. The absolute value corresponds to the `errno` code.
    *   **libc Responsibility**: The C library wrapper is responsible for checking this range, negating the value to set `errno`, and returning `-1` to the application.

### Argument Passing
Substrate Native uses **Stack Conventions** (similar to FreeBSD/SVR4), distinct from Linux's register-based convention.

*   Arguments are pushed onto the user stack.
*   The kernel expects arguments starting at `ESP + 4`.
*   The value at `ESP` is ignored by the kernel (conventionally the return address).

**Layout at `int 0x80`:**
```
[High Address]
Argument N
...
Argument 2
Argument 1
Return Address (Ignored) <--- ESP
[Low Address]
```

### Syscall Table

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

*(Note: Gaps in numbering correspond to reserved or unimplemented legacy slots)*

## 2. Data Structures

### File Metadata (`struct stat`)
Defined in `<sys/stat.h>`. **Size: 92 bytes.**

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

### System Info (`struct utsname`)
Defined in `<sys/utsname.h>`.

| Field        | Type        | Size (bytes) | Description                         |
| :---         | :---        | :---         | :---                                |
| `sysname`    | `char[]`    | 65           | Operating System Name ("substrate") |
| `nodename`   | `char[]`    | 65           | Network Node Name                   |
| `release`    | `char[]`    | 65           | Release Level                       |
| `version`    | `char[]`    | 65           | Version Level                       |
| `machine`    | `char[]`    | 65           | Hardware Type                       |
| `domainname` | `char[]`    | 65           | NIS Domain Name                     |

### Time Structures
Defined in `<sys/time.h>`. `time_t` is a 64-bit signed integer.

**`struct timespec`**
| Field     | Type     | Size | Description |
| :---      | :---     | :--- | :---        |
| `tv_sec`  | `time_t` | 8    | Seconds     |
| `tv_nsec` | `long`   | 4    | Nanoseconds |

**`struct timeval`**
| Field     | Type          | Size | Description  |
| :---      | :---          | :--- | :---         |
| `tv_sec`  | `time_t`      | 8    | Seconds      |
| `tv_usec` | `suseconds_t` | 4    | Microseconds |

### Polling (`struct pollfd`)
Defined in `<sys/poll.h>`.

| Field     | Type    | Size | Description      |
| :---      | :---    | :--- | :---             |
| `fd`      | `int`   | 4    | File Descriptor  |
| `events`  | `short` | 2    | Requested Events |
| `revents` | `short` | 2    | Returned Events  |

### Directory Entry (`struct dirent`)
Used by `getdents`. Variable length structure.

| Field      | Type             | Size | Description                                  |
| :---       | :---             | :--- | :---                                         |
| `d_ino`    | `unsigned long`  | 4    | Inode number                                 |
| `d_off`    | `unsigned long`  | 4    | Offset to next entry                         |
| `d_reclen` | `unsigned short` | 2    | Length of this record                        |
| `d_name`   | `char[]`         | Var  | Filename (null-terminated)                   |

### Thread Creation (`struct thr_param`)
Used by `thr_new`.

| Field        | Type        | Size | Description                      |
| :---         | :---        | :--- | :---                             |
| `start_func` | `ptr`       | 4    | Entry point function             |
| `arg`        | `ptr`       | 4    | Argument for entry point         |
| `stack_base` | `ptr`       | 4    | Stack base address               |
| `stack_size` | `size_t`    | 4    | Stack size in bytes              |
| `tls_base`   | `ptr`       | 4    | TLS segment base address         |
| `tls_size`   | `size_t`    | 4    | TLS segment size                 |
| `child_tid`  | `long*`     | 4    | Address to write Child TID       |
| `parent_tid` | `long*`     | 4    | Address to write Parent TID      |
| `flags`      | `int`       | 4    | Creation flags                   |

### Signal Stack (`stack_t`)
Used by `sigaltstack`.

| Field      | Type     | Size | Description        |
| :---       | :---     | :--- | :---               |
| `ss_sp`    | `void*`  | 4    | Stack base/pointer |
| `ss_flags` | `int`    | 4    | Flags (__SS_DISABLE__, etc) |
| `ss_size`  | `size_t` | 4    | Stack size         |

---

## 3. Process Initialization

When a new process is started `EIP` points to the entry point, `ESP` points to arguments.

### Stack Layout (Top to Bottom)

The stack grows downwards.

| Content             | Description                                                                           |
| :---                | :---                                                                                  |
| Strings             | String data for Envp, Argv, Platform, etc.                                            |
| `AT_NULL`           | `{0, 0}` Auxv Terminator                                                              |
| Auxv Entries        | Elf Auxiliary Vector entries (Page size, PHDR, Entry, UID/GID, Random, Platform, etc.)|
| `AT_RANDOM` data    | 16 bytes of random data for the canary                                                |
| `Envp[]`            | Array of pointers to environment strings, suffixed with `NULL`                        |
| `Argv[]`            | Array of pointers to argument strings, suffixed with `NULL`                           |
| `Argc`              | Integer argument count                                                                |
| **Stack Pointer**   | `ESP` points here at entry                                                            |

## 4. Signal Handling

### Signal Frame
*   **Trampoline**: The return address pushed to the stack points to the signal trampoline.
*   **Alignment**: Stack is aligned to 16 bytes.
