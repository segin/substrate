# 14. Native 64-bit Stat ABI Enforcement

> This file was seeded from `TASKS.md` using a fork-copy (rename+restore) workflow to preserve lineage.
> Source span in original monolith: lines 10345-10439.

## Reimplemented Checklist (All Open)

### 14. Native 64-bit Stat ABI Enforcement
Reference: User Request (Step 30668)

- [ ] **ABI Definition & Documentation:** (REQ: REQ-18-0001)
    - [ ] Define and document the canonical 64-bit `struct stat` and kernel syscall ABI. (REQ: REQ-18-0002)
        - Files: `sys/include/sys/stat.h`, `sys/doc/abi.md`
        - Tests: property (offsets/sizes verification)
        - Docs: Developer guide, `stat.2`
        - Acceptance: Single 64-bit definition in public header, ABI fully documented
    - [ ] Create manpages for `stat(2)` explaining single-ABI policy. (REQ: REQ-18-0003)
        - Files: `usr/man/man2/stat.2`
        - Tests: doc validation
        - Docs: `stat.2`
        - Acceptance: Manpage documents 64-bit nature and usage

- [ ] **LibC Updates:** (REQ: REQ-18-0004)
    - [ ] Update LibC `stat` wrappers to use canonical 64-bit ABI (all architectures). (REQ: REQ-18-0005)
        - Files: `lib/c/src/sys.c`, `lib/c/include/sys/stat.h`
        - Tests: unit (struct size validation on i386 and amd64)
        - Docs: LibC internal docs
        - Acceptance: Userspace calls map directly to 64-bit kernel structure
    - [ ] Update `fstat`, `lstat`, `fstatat` to use 64-bit ABI. (REQ: REQ-18-0006)
        - Files: `lib/c/src/sys.c`
        - Tests: property (fd/path consistency)
        - Docs: `fstat.2`, `lstat.2`
        - Acceptance: All stat-family functions use new ABI

- [ ] **Kernel Implementation:** (REQ: REQ-18-0007)
    - [ ] Expose ONLY canonical 64-bit stat syscalls for native personality. (REQ: REQ-18-0008)
        - Files: `sys/exec/perso/perso_native.c`, `sys/arch/i386/syscall.c`
        - Tests: integration (strace confirms correct syscall usage)
        - Docs: `native_abi.md`
        - Acceptance: Native table has no legacy 32-bit stat entries
    - [ ] Audit and remove "stub-like" stat implementations. (REQ: REQ-18-0009)
        - Files: `sys/kern/vfs_syscalls.c` (or equivalent)
        - Tests: unit (error handling, edge cases)
        - Docs: Source comments
        - Acceptance: Full implementation with robust error paths
    - [ ] Add compatibility shims for non-native personalities (Linux/FreeBSD) only where needed. (REQ: REQ-18-0010)
        - Files: `sys/exec/perso/perso_linux.c`, `sys/exec/perso/perso_freebsd.c`
        - Tests: integration (compat shim translates correctly)
        - Docs: Personality internal docs
        - Acceptance: Foreign binaries work, native binaries use clean 64-bit path

- [ ] **Testing & Verification:** (REQ: REQ-18-0011)
    - [ ] Create ABI regression tools to assert field offsets and sizes. (REQ: REQ-18-0012)
        - Files: `tests/abi/stat_test.c`
        - Tests: property (offset assert)
        - Docs: Test README
        - Acceptance: Tool compiled and running on CI
    - [ ] Property tests for large files, sparse files, and odd timestamps. (REQ: REQ-18-0013)
        - Files: `tests/fs/stat_properties.c`
        - Tests: property (fuzz inputs)
        - Docs: Test plan
        - Acceptance: Tests pass consistently
    - [ ] Audit and listing of userland tools using `stat`. (REQ: REQ-18-0014)
        - Files: `bin/*`
        - Tests: N/A
        - Docs: `audit_stat_users.md`
        - Acceptance: all call sites identified
    - [ ] Update userland tools to 64-bit stat. (REQ: REQ-18-0015)
        - Files: `bin/ls.c`, `bin/tar.c`, etc.
        - Tests: integration (ls -l correct output)
        - Docs: N/A
        - Acceptance: Tools built against new LibC

- [ ] **CI & Tooling:** (REQ: REQ-18-0016)
    - [ ] Update CI to build and test both 32-bit and 64-bit targets. (REQ: REQ-18-0017)
        - Files: `Makefile`, `.github/workflows/ci.yml`
        - Tests: CI pipeline
        - Docs: CI Reference
        - Acceptance: Both targets green, confirming 64-bit ABI works on 32-bit arch

- [ ] **Extended 64-bit Sycall ABI (Beyond Stat):** (REQ: REQ-18-0018)
    - [ ] Standardize `lseek` / `lseek64` to single 64-bit offset ABI. (REQ: REQ-18-0019)
        - Files: `sys/arch/i386/syscall.c`
        - Tests: property (seek beyond 2GB)
        - Acceptance: `lseek` handles 64-bit offsets natively.
    - [ ] Standardize `truncate` / `ftruncate` to 64-bit ABI. (REQ: REQ-18-0020)
        - Files: `sys/kern/vfs_syscalls.c`
        - Tests: property (truncate large file)
        - Acceptance: `truncate` / `ftruncate` are natively 64-bit; no `truncate64` syscall needed.
    - [ ] Standardize `mmap` to handle 64-bit offsets (pgoff). (REQ: REQ-18-0021)
        - Files: `sys/arch/i386/syscall.c`
        - Tests: integration (map large offset)
        - Acceptance: `mmap` accepts 64-bit offset (or sufficient page count).
    - [ ] Review `getdents` / `getdents64` dirent structures. (REQ: REQ-18-0022)
        - Files: `sys/fs/fs.c`
        - Tests: integration (read directory with many/large inodes)
        - Acceptance: Single 64-bit friendly dirent format (`getdents` implies 64-bit inodes/offsets).
    - [ ] Standardize `statfs` / `statvfs` to 64-bit block counts. (REQ: REQ-18-0023)
        - Files: `sys/vfs/vfs.c`
        - Tests: integration (df on large volume)
        - Acceptance: Report correct size for >2TB volumes.


## User Stories

- **US-18-0001**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to aBI Definition & Documentation: so that this capability is implemented with clear verification evidence.
- **US-18-0002**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to define and document the canonical 64-bit struct stat and kernel syscall ABI so that this capability is implemented with clear verification evidence.
- **US-18-0003**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to create manpages for stat(2) explaining single-ABI policy so that this capability is implemented with clear verification evidence.
- **US-18-0004**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to libC Updates: so that this capability is implemented with clear verification evidence.
- **US-18-0005**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to update LibC stat wrappers to use canonical 64-bit ABI (all architectures) so that this capability is implemented with clear verification evidence.
- **US-18-0006**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to update fstat, lstat, fstatat to use 64-bit ABI so that this capability is implemented with clear verification evidence.
- **US-18-0007**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to kernel Implementation: so that this capability is implemented with clear verification evidence.
- **US-18-0008**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to expose ONLY canonical 64-bit stat syscalls for native personality so that this capability is implemented with clear verification evidence.
- **US-18-0009**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to audit and remove "stub-like" stat implementations so that this capability is implemented with clear verification evidence.
- **US-18-0010**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to add compatibility shims for non-native personalities (Linux/FreeBSD) only where needed so that this capability is implemented with clear verification evidence.
- **US-18-0011**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to testing & Verification: so that this capability is implemented with clear verification evidence.
- **US-18-0012**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to create ABI regression tools to assert field offsets and sizes so that this capability is implemented with clear verification evidence.
- **US-18-0013**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to property tests for large files, sparse files, and odd timestamps so that this capability is implemented with clear verification evidence.
- **US-18-0014**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to audit and listing of userland tools using stat so that this capability is implemented with clear verification evidence.
- **US-18-0015**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to update userland tools to 64-bit stat so that this capability is implemented with clear verification evidence.
- **US-18-0016**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to cI & Tooling: so that this capability is implemented with clear verification evidence.
- **US-18-0017**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to update CI to build and test both 32-bit and 64-bit targets so that this capability is implemented with clear verification evidence.
- **US-18-0018**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to extended 64-bit Sycall ABI (Beyond Stat): so that this capability is implemented with clear verification evidence.
- **US-18-0019**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to standardize lseek / lseek64 to single 64-bit offset ABI so that this capability is implemented with clear verification evidence.
- **US-18-0020**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to standardize truncate / ftruncate to 64-bit ABI so that this capability is implemented with clear verification evidence.
- **US-18-0021**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to standardize mmap to handle 64-bit offsets (pgoff) so that this capability is implemented with clear verification evidence.
- **US-18-0022**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to review getdents / getdents64 dirent structures so that this capability is implemented with clear verification evidence.
- **US-18-0023**: As a Substrate contributor working on 14. Native 64-bit Stat ABI Enforcement, I want to standardize statfs / statvfs to 64-bit block counts so that this capability is implemented with clear verification evidence.

## INCOSE/EARS Requirements

- **REQ-18-0001** (EARS/Ubiquitous): The Substrate system shall aBI Definition & Documentation:.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0002** (EARS/Ubiquitous): The Substrate system shall define and document the canonical 64-bit struct stat and kernel syscall ABI.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0003** (EARS/Ubiquitous): The Substrate system shall create manpages for stat(2) explaining single-ABI policy.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0004** (EARS/Ubiquitous): The Substrate system shall libC Updates:.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0005** (EARS/Ubiquitous): The Substrate system shall update LibC stat wrappers to use canonical 64-bit ABI (all architectures).
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0006** (EARS/Ubiquitous): The Substrate system shall update fstat, lstat, fstatat to use 64-bit ABI.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0007** (EARS/Ubiquitous): The Substrate system shall kernel Implementation:.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0008** (EARS/Ubiquitous): The Substrate system shall expose ONLY canonical 64-bit stat syscalls for native personality.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0009** (EARS/Ubiquitous): The Substrate system shall audit and remove "stub-like" stat implementations.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0010** (EARS/Ubiquitous): The Substrate system shall add compatibility shims for non-native personalities (Linux/FreeBSD) only where needed.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0011** (EARS/Ubiquitous): The Substrate system shall testing & Verification:.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0012** (EARS/Ubiquitous): The Substrate system shall create ABI regression tools to assert field offsets and sizes.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0013** (EARS/Ubiquitous): The Substrate system shall property tests for large files, sparse files, and odd timestamps.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0014** (EARS/Ubiquitous): The Substrate system shall audit and listing of userland tools using stat.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0015** (EARS/Ubiquitous): The Substrate system shall update userland tools to 64-bit stat.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0016** (EARS/Ubiquitous): The Substrate system shall cI & Tooling:.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0017** (EARS/Ubiquitous): The Substrate system shall update CI to build and test both 32-bit and 64-bit targets.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0018** (EARS/Ubiquitous): The Substrate system shall extended 64-bit Sycall ABI (Beyond Stat):.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0019** (EARS/Ubiquitous): The Substrate system shall standardize lseek / lseek64 to single 64-bit offset ABI.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0020** (EARS/Ubiquitous): The Substrate system shall standardize truncate / ftruncate to 64-bit ABI.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0021** (EARS/Ubiquitous): The Substrate system shall standardize mmap to handle 64-bit offsets (pgoff).
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0022** (EARS/Ubiquitous): The Substrate system shall review getdents / getdents64 dirent structures.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
- **REQ-18-0023** (EARS/Ubiquitous): The Substrate system shall standardize statfs / statvfs to 64-bit block counts.
  - Context: 14. Native 64-bit Stat ABI Enforcement
  - Verification: design review + implementation evidence + test/doc update.
